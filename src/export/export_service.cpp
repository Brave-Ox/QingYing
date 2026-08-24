#include "qingying/export/export_service.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "qingying/export/dib_encoder.hpp"

namespace qingying {

namespace {

struct GlobalMemoryDeleter {
  void operator()(void* handle) const {
    if (handle != nullptr) {
      ::GlobalFree(static_cast<HGLOBAL>(handle));
    }
  }
};

ActionResult makeExportError(const char* message) {
  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kExportFailed;
  r.message = message;
  return r;
}

class ScopedComInitialization {
 public:
  ScopedComInitialization()
      : result_(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}

  ~ScopedComInitialization() {
    if (result_ == S_OK || result_ == S_FALSE) {
      ::CoUninitialize();
    }
  }

  bool usable() const {
    // RPC_E_CHANGED_MODE means this thread already has a different COM
    // apartment. Existing COM initialization is still usable by WIC.
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }

 private:
  HRESULT result_;
};

}  // namespace

ActionResult ExportService::copyToClipboard(const Image& image) {
  const std::vector<std::uint8_t> dib_bytes = dib::encodeDib(image);
  if (dib_bytes.empty()) {
    return makeExportError("ExportService::copyToClipboard: 空图，无内容可复制");
  }

  if (!::OpenClipboard(nullptr)) {
    return makeExportError("ExportService::copyToClipboard: OpenClipboard 失败");
  }

  if (!::EmptyClipboard()) {
    ::CloseClipboard();
    return makeExportError("ExportService::copyToClipboard: EmptyClipboard 失败");
  }

  std::unique_ptr<void, GlobalMemoryDeleter> h_mem(
      ::GlobalAlloc(GMEM_MOVEABLE, dib_bytes.size()));
  if (!h_mem) {
    ::CloseClipboard();
    return makeExportError("ExportService::copyToClipboard: GlobalAlloc 失败");
  }

  void* const locked = ::GlobalLock(h_mem.get());
  if (locked == nullptr) {
    ::CloseClipboard();
    return makeExportError("ExportService::copyToClipboard: GlobalLock 失败");
  }
  std::memcpy(locked, dib_bytes.data(), dib_bytes.size());
  // 单次 GlobalLock 配对单次 GlobalUnlock：此时返回 FALSE 仅为"仍有锁计数"
  // 的良性情况（GetLastError()==NO_ERROR），无需按错误处理。
  (void)::GlobalUnlock(h_mem.get());

  if (::SetClipboardData(CF_DIB, h_mem.get()) == nullptr) {
    // 失败：系统未接管句柄，交由 unique_ptr 的删除器 GlobalFree。
    ::CloseClipboard();
    return makeExportError("ExportService::copyToClipboard: SetClipboardData 失败");
  }

  // 成功：系统接管 h_mem 所有权，release 防止二次 GlobalFree。
  h_mem.release();
  ::CloseClipboard();

  ActionResult r;
  r.ok = true;
  r.error_code = ErrorCode::kOk;
  r.message = "图像已复制到剪贴板";
  return r;
}

ActionResult ExportService::savePng(const Image& image,
                                    const std::wstring& path) {
  if (path.empty()) {
    ActionResult r;
    r.ok = false;
    r.error_code = ErrorCode::kInvalidArgument;
    r.message = "ExportService::savePng: 保存路径不能为空";
    return r;
  }

  if (image.width <= 0 || image.height <= 0) {
    return makeExportError("ExportService::savePng: 空图，无内容可保存");
  }

  const std::size_t width = static_cast<std::size_t>(image.width);
  const std::size_t height = static_cast<std::size_t>(image.height);
  if (height > (std::numeric_limits<std::size_t>::max)() / width) {
    return makeExportError("ExportService::savePng: 图像尺寸溢出");
  }
  const std::size_t pixel_count = width * height;
  if (image.pixels.size() < pixel_count ||
      width > (std::numeric_limits<std::size_t>::max)() / 4u) {
    return makeExportError("ExportService::savePng: 像素数据不足");
  }

  const std::size_t stride = width * 4u;
  if (height > (std::numeric_limits<std::size_t>::max)() / stride ||
      stride * height > (std::numeric_limits<UINT>::max)()) {
    return makeExportError("ExportService::savePng: 图像缓冲区过大");
  }

  ScopedComInitialization com;
  if (!com.usable()) {
    return makeExportError("ExportService::savePng: COM 初始化失败");
  }

  using Microsoft::WRL::ComPtr;
  ComPtr<IWICImagingFactory> factory;
  HRESULT hr = ::CoCreateInstance(
      CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(factory.GetAddressOf()));
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 创建 WIC 工厂失败");
  }

  ComPtr<IWICStream> stream;
  hr = factory->CreateStream(stream.GetAddressOf());
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 创建 WIC 流失败");
  }
  hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 打开输出文件失败");
  }

  ComPtr<IWICBitmapEncoder> encoder;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr,
                              encoder.GetAddressOf());
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 创建 PNG 编码器失败");
  }
  hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 初始化 PNG 编码器失败");
  }

  ComPtr<IWICBitmapFrameEncode> frame;
  ComPtr<IPropertyBag2> properties;
  hr = encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf());
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 创建 PNG 帧失败");
  }
  hr = frame->Initialize(properties.Get());
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 初始化 PNG 帧失败");
  }
  hr = frame->SetSize(static_cast<UINT>(image.width),
                      static_cast<UINT>(image.height));
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 设置 PNG 尺寸失败");
  }

  WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
  hr = frame->SetPixelFormat(&pixel_format);
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 设置 PNG 像素格式失败");
  }

  ComPtr<IWICBitmap> bitmap;
  hr = factory->CreateBitmapFromMemory(
      static_cast<UINT>(image.width), static_cast<UINT>(image.height),
      GUID_WICPixelFormat32bppBGRA, static_cast<UINT>(stride),
      static_cast<UINT>(stride * height),
      reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(image.pixels.data())),
      bitmap.GetAddressOf());
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 创建 WIC 位图失败");
  }

  hr = frame->WriteSource(bitmap.Get(), nullptr);
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 写入 PNG 像素失败");
  }
  hr = frame->Commit();
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 提交 PNG 帧失败");
  }
  hr = encoder->Commit();
  if (FAILED(hr)) {
    return makeExportError("ExportService::savePng: 提交 PNG 文件失败");
  }

  ActionResult r;
  r.ok = true;
  r.error_code = ErrorCode::kOk;
  r.message = "图像已保存为 PNG";
  return r;
}

}  // namespace qingying
