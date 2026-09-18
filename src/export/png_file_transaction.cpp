#include "png_file_transaction.h"

#include <Windows.h>
#include <objidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>

namespace qingying::detail {
namespace {

using Microsoft::WRL::ComPtr;

ActionResult failure(int code, const char* message, const char* stage) {
  ActionResult result;
  result.error_code = code;
  result.message = message;
  result.failure_stage = stage;
  return result;
}

class Handle final {
 public:
  Handle() = default;
  explicit Handle(HANDLE value) : value_(value) {}
  ~Handle() { reset(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  Handle(Handle&& other) noexcept : value_(other.release()) {}
  Handle& operator=(Handle&& other) noexcept {
    if (this != &other) reset(other.release());
    return *this;
  }
  explicit operator bool() const noexcept {
    return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
  }
  HANDLE get() const noexcept { return value_; }
  HANDLE release() noexcept {
    HANDLE value = value_;
    value_ = INVALID_HANDLE_VALUE;
    return value;
  }
  void reset(HANDLE value = INVALID_HANDLE_VALUE) noexcept {
    if (*this) CloseHandle(value_);
    value_ = value;
  }
 private:
  HANDLE value_{INVALID_HANDLE_VALUE};
};

class TemporaryFile final {
 public:
  TemporaryFile(std::wstring path, Handle handle)
      : path_(std::move(path)), handle_(std::move(handle)) {}
  ~TemporaryFile() {
    handle_.reset();
    if (!committed_ && !path_.empty()) DeleteFileW(path_.c_str());
  }
  Handle& handle() noexcept { return handle_; }
  const std::wstring& path() const noexcept { return path_; }
  void committed() noexcept { committed_ = true; }
 private:
  std::wstring path_;
  Handle handle_;
  bool committed_{false};
};

class ComInitialization final {
 public:
  ComInitialization() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComInitialization() {
    if (result_ == S_OK || result_ == S_FALSE) CoUninitialize();
  }
  bool usable() const noexcept {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }
 private:
  HRESULT result_;
};

// WIC writes directly into the already-exclusive temporary file. No complete
// PNG is accumulated in an HGLOBAL or copied a second time before commit.
class PngFileStream final : public IStream {
 public:
  explicit PngFileStream(HANDLE file) : file_(file) {}
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** value) override {
    if (!value) return E_POINTER;
    *value = nullptr;
    if (id != IID_IUnknown && id != IID_ISequentialStream && id != IID_IStream) return E_NOINTERFACE;
    *value = static_cast<IStream*>(this); AddRef(); return S_OK;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto count = --references_; if (!count) delete this; return count;
  }
  HRESULT STDMETHODCALLTYPE Read(void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE Write(const void* data, ULONG size, ULONG* written) override {
    if (written) *written = 0;
    if (!data && size) return STG_E_INVALIDPOINTER;
    auto* bytes = static_cast<const BYTE*>(data);
    ULONG total = 0;
    while (total < size) {
      DWORD count = 0;
      if (!WriteFile(file_, bytes + total, (std::min)(size - total, 1024UL * 1024), &count, nullptr) || !count)
        return STG_E_WRITEFAULT;
      total += count;
      if (written) *written = total;
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER offset, DWORD origin, ULARGE_INTEGER* position) override {
    if (origin > STREAM_SEEK_END) return STG_E_INVALIDFUNCTION;
    LARGE_INTEGER result{};
    if (!SetFilePointerEx(file_, offset, &result, origin)) return STG_E_SEEKERROR;
    if (position) position->QuadPart = static_cast<ULONGLONG>(result.QuadPart);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER size) override {
    if (size.QuadPart > static_cast<ULONGLONG>((std::numeric_limits<LONGLONG>::max)())) return STG_E_MEDIUMFULL;
    LARGE_INTEGER zero{}, saved{}, end{};
    end.QuadPart = static_cast<LONGLONG>(size.QuadPart);
    if (!SetFilePointerEx(file_, zero, &saved, FILE_CURRENT) ||
        !SetFilePointerEx(file_, end, nullptr, FILE_BEGIN) || !SetEndOfFile(file_)) return STG_E_WRITEFAULT;
    return SetFilePointerEx(file_, saved, nullptr, FILE_BEGIN) ? S_OK : STG_E_SEEKERROR;
  }
  HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return FlushFileBuffers(file_) ? S_OK : STG_E_WRITEFAULT; }
  HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Stat(STATSTG* stat, DWORD) override {
    if (!stat) return E_POINTER;
    *stat = {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file_, &size)) return STG_E_READFAULT;
    stat->type = STGTY_STREAM; stat->grfMode = STGM_WRITE;
    stat->cbSize.QuadPart = static_cast<ULONGLONG>(size.QuadPart); return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Clone(IStream** value) override { if (value) *value = nullptr; return E_NOTIMPL; }
 private:
  std::atomic<ULONG> references_{1};
  HANDLE file_;
};

bool validImage(const Image& image, UINT* stride, UINT* bytes) {
  if (image.width <= 0 || image.height <= 0) return false;
  const auto width = static_cast<std::size_t>(image.width);
  const auto height = static_cast<std::size_t>(image.height);
  if (width > (std::numeric_limits<UINT>::max)() / 4u) return false;
  const auto row = width * 4u;
  if (height > (std::numeric_limits<UINT>::max)() / row) return false;
  if (height > (std::numeric_limits<std::size_t>::max)() / width ||
      image.pixels.size() < width * height) return false;
  *stride = static_cast<UINT>(row);
  *bytes = static_cast<UINT>(row * height);
  return true;
}

HRESULT encodePng(const Image& image, IStream* stream) {
  UINT stride = 0;
  UINT bytes = 0;
  if (!validImage(image, &stride, &bytes)) return E_INVALIDARG;

  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
  if (FAILED(hr)) return hr;
  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                              encoder.GetAddressOf());
  if (FAILED(hr)) return hr;
  hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
  if (FAILED(hr)) return hr;
  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> properties;
  hr = encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf());
  if (FAILED(hr)) return hr;
  hr = frame->Initialize(properties.Get());
  if (FAILED(hr)) return hr;
  hr = frame->SetSize(static_cast<UINT>(image.width),
                      static_cast<UINT>(image.height));
  if (FAILED(hr)) return hr;
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  hr = frame->SetPixelFormat(&format);
  if (FAILED(hr) || format != GUID_WICPixelFormat32bppBGRA) return E_FAIL;
  hr = frame->WritePixels(static_cast<UINT>(image.height), stride, bytes,
      reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data())));
  if (FAILED(hr)) return hr;
  hr = frame->Commit();
  return SUCCEEDED(hr) ? encoder->Commit() : hr;
}

std::unique_ptr<TemporaryFile> createTemporary(const std::wstring& directory) {
  static std::atomic_uint64_t sequence{0};
  for (unsigned attempt = 0; attempt < 64; ++attempt) {
    const auto value = sequence.fetch_add(1, std::memory_order_relaxed);
    const std::wstring name = L".qingying-" + std::to_wstring(GetCurrentProcessId()) +
        L"-" + std::to_wstring(value) + L".tmp";
    const auto path = (std::filesystem::path(directory) / name).wstring();
    Handle handle(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, nullptr));
    if (handle) return std::make_unique<TemporaryFile>(path, std::move(handle));
    if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
      return {};
  }
  return {};
}

}  // namespace

ActionResult savePngFileTransaction(const Image& image,
    const std::wstring& path, ExportService::PngSaveOptions options) try {
  if (path.empty()) return failure(ErrorCode::kInvalidArgument,
      "save path is required", "validate");
  UINT stride = 0;
  UINT bytes = 0;
  if (!validImage(image, &stride, &bytes)) return failure(ErrorCode::kExportFailed,
      "invalid image for PNG export", "encode");

  const std::filesystem::path destination(path);
  const auto directory = destination.parent_path();
  if (directory.empty() || destination.filename().empty()) return failure(
      ErrorCode::kInvalidArgument, "absolute output path is required", "validate");

  Handle directory_handle(CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!directory_handle) return failure(ErrorCode::kAccessDenied,
      "output directory cannot be opened", "validate_directory");
  FILE_ATTRIBUTE_TAG_INFO tag{};
  if (!GetFileInformationByHandleEx(directory_handle.get(), FileAttributeTagInfo,
          &tag, sizeof(tag)) || (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      (tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
    return failure(ErrorCode::kAccessDenied,
        "output directory identity is not allowed", "validate_directory");
  }

  // Conservatively admit WIC scratch/conversion buffers before codec entry.
  // File-backed output avoids an additional full compressed-image allocation.
  auto encode_memory = ImageMemoryBudget::global().reserve(
      static_cast<std::uint64_t>(bytes) * 2 + stride * 8ULL + 65536,
      ImageMemoryKind::EncodeScratch);
  if (!encode_memory) return failure(ErrorCode::kResourceLimit,
      "PNG encoder exceeds image memory budget", "encode");
  auto temporary = createTemporary(directory.wstring());
  if (!temporary) return failure(ErrorCode::kExportFailed,
      "failed to create exclusive temporary file", "create_temporary");

  ComInitialization com;
  if (!com.usable()) return failure(ErrorCode::kExportFailed,
      "COM initialization failed", "encode");
  ComPtr<IStream> stream;
  stream.Attach(new PngFileStream(temporary->handle().get()));
  HRESULT hr = encodePng(image, stream.Get());
  if (FAILED(hr)) return failure(ErrorCode::kExportFailed,
      "PNG encoding failed", "encode");
  if (FAILED(stream->Commit(STGC_DEFAULT))) {
    return failure(ErrorCode::kExportFailed, "temporary PNG write failed", "write_temporary");
  }
  stream.Reset();

  if (options.authorize_commit && !options.authorize_commit())
    return failure(ErrorCode::kCancelled, "save cancelled before commit", "commit_authorization");

  temporary->handle().reset();
  const DWORD flags = MOVEFILE_WRITE_THROUGH |
      (options.overwrite ? MOVEFILE_REPLACE_EXISTING : 0);
  if (!MoveFileExW(temporary->path().c_str(), destination.c_str(), flags)) {
    const DWORD error = GetLastError();
    return failure(error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
                       ? ErrorCode::kConflict : ErrorCode::kExportFailed,
        error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
            ? "destination already exists" : "atomic PNG commit failed",
        "commit");
  }
  temporary->committed();

  ActionResult result;
  result.ok = true;
  result.error_code = ErrorCode::kOk;
  result.message = "image saved as PNG";
  result.output = SavedResult{kInvalidResultId,
      std::filesystem::absolute(destination).lexically_normal().wstring(),
      ImageFormat::Png};
  return result;
} catch (const std::bad_alloc&) {
  return failure(ErrorCode::kResourceLimit, "image memory budget exhausted", "encode");
}

}  // namespace qingying::detail
