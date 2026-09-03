#include "browser_capture_receiver.hpp"

#include "qingying/longshot/longshot_messages.hpp"

#include <Windows.h>
#include <wincodec.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace qingying {
namespace {

constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\QingYingBrowserCaptureV1";
constexpr DWORD kMaxPathBytes = 32768;

bool readExact(HANDLE pipe, void* buffer, DWORD bytes) {
  auto* cursor = static_cast<BYTE*>(buffer);
  DWORD read_total = 0;
  while (read_total < bytes) {
    DWORD read = 0;
    if (!ReadFile(pipe, cursor + read_total, bytes - read_total, &read, nullptr) ||
        read == 0) return false;
    read_total += read;
  }
  return true;
}

bool decodePng(const std::wstring& path, Image& image) {
  image = {};
  IWICImagingFactory* factory = nullptr;
  IWICBitmapDecoder* decoder = nullptr;
  IWICBitmapFrameDecode* frame = nullptr;
  IWICFormatConverter* converter = nullptr;
  const HRESULT created = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
      CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (FAILED(created)) return false;
  const HRESULT decoded = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
      GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
  if (SUCCEEDED(decoded)) decoder->GetFrame(0, &frame);
  if (frame != nullptr) factory->CreateFormatConverter(&converter);
  UINT width = 0, height = 0;
  if (converter != nullptr && SUCCEEDED(frame->GetSize(&width, &height)) &&
      width > 0 && height > 0 && width <= 16384 && height <= 65535 &&
      SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
          WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) {
    Image decoded_image;
    decoded_image.width = static_cast<int>(width);
    decoded_image.height = static_cast<int>(height);
    decoded_image.pixels.resize(static_cast<size_t>(width) * height);
    if (SUCCEEDED(converter->CopyPixels(nullptr, width * 4,
        static_cast<UINT>(decoded_image.pixels.size() * sizeof(std::uint32_t)),
        reinterpret_cast<BYTE*>(decoded_image.pixels.data())))) image = std::move(decoded_image);
  }
  if (converter) converter->Release();
  if (frame) frame->Release();
  if (decoder) decoder->Release();
  factory->Release();
  return !image.empty();
}
}  // namespace

struct BrowserCaptureReceiver::Impl {
  std::atomic<bool> stopping{false};
  std::thread worker;
  HWND notification_window{nullptr};
  std::mutex mutex;
  Image pending;

  void run() {
    (void)CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    while (!stopping.load()) {
      HANDLE pipe = CreateNamedPipeW(kPipeName, PIPE_ACCESS_INBOUND,
          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 0, kMaxPathBytes,
          0, nullptr);
      if (pipe == INVALID_HANDLE_VALUE) break;
      const BOOL connected = ConnectNamedPipe(pipe, nullptr) ||
          GetLastError() == ERROR_PIPE_CONNECTED;
      DWORD length = 0;
      std::string utf8_path;
      if (connected && readExact(pipe, &length, sizeof(length)) && length > 0 &&
          length <= kMaxPathBytes) {
        utf8_path.resize(length);
        if (readExact(pipe, utf8_path.data(), length)) {
          const int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
              utf8_path.data(), static_cast<int>(utf8_path.size()), nullptr, 0);
          if (chars > 0) {
            std::wstring path(chars, L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path.data(),
                static_cast<int>(utf8_path.size()), path.data(), chars);
            Image image;
            if (decodePng(path, image)) {
              std::lock_guard<std::mutex> lock(mutex);
              pending = std::move(image);
              PostMessageW(notification_window,
                           WM_QINGYING_LONGSHOT_EXTERNAL_RESULT, 0, 0);
            }
            DeleteFileW(path.c_str());
          }
        }
      }
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
    }
    CoUninitialize();
  }
};

BrowserCaptureReceiver::BrowserCaptureReceiver() : impl_(std::make_unique<Impl>()) {}
BrowserCaptureReceiver::~BrowserCaptureReceiver() { stop(); }
bool BrowserCaptureReceiver::start(HWND window) {
  if (window == nullptr || impl_->worker.joinable()) return false;
  impl_->notification_window = window;
  impl_->stopping.store(false);
  impl_->worker = std::thread([this] { impl_->run(); });
  return true;
}
void BrowserCaptureReceiver::stop() noexcept {
  if (!impl_) return;
  impl_->stopping.store(true);
  HANDLE wake = CreateFileW(kPipeName, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);
  if (impl_->worker.joinable()) impl_->worker.join();
}
bool BrowserCaptureReceiver::takeImage(Image& image) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (impl_->pending.empty()) return false;
  image = std::move(impl_->pending);
  return true;
}
}  // namespace qingying
