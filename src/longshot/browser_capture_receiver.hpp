#pragma once

#include "qingying/action/image.hpp"

#include <Windows.h>

#include <memory>

namespace qingying {

// Chrome Native Messaging 的传输实现只在 longshot 内部使用。
class BrowserCaptureReceiver {
 public:
  BrowserCaptureReceiver();
  ~BrowserCaptureReceiver();
  BrowserCaptureReceiver(const BrowserCaptureReceiver&) = delete;
  BrowserCaptureReceiver& operator=(const BrowserCaptureReceiver&) = delete;

  bool start(HWND notification_window);
  void stop() noexcept;
  bool takeImage(Image& image);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qingying
