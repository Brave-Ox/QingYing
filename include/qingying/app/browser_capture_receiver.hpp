#pragma once

#include "qingying/action/image.hpp"

#include <Windows.h>

#include <memory>

namespace qingying {

// Receives a PNG filename from the local Chrome Native Messaging host. The
// listener is local-machine only and hands decoded images back to Application
// through its normal Win32 message loop.
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
