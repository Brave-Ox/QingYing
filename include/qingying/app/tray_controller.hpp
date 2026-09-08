#pragma once

#include <Windows.h>

#include <functional>

namespace qingying {

class TrayController {
 public:
  using MessageFilter =
      std::function<bool(UINT msg, WPARAM wparam, LPARAM lparam, LRESULT* result)>;

  TrayController();
  ~TrayController();

  TrayController(const TrayController&) = delete;
  TrayController& operator=(const TrayController&) = delete;

  bool create(HINSTANCE instance);
  void destroy();

  void setMessageFilter(MessageFilter filter);
  void setAutomationToggle(std::function<bool(bool)> toggle) { automation_toggle_ = std::move(toggle); }
  void setAutomationEnabled(bool enabled) { automation_enabled_ = enabled; }

  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT handleMessage(UINT msg, WPARAM wparam, LPARAM lparam);

  void showContextMenu();
  void onCommand(UINT id);
  bool addTrayIcon();
  void removeTrayIcon();
  HICON loadAppIcon(HINSTANCE instance);

  HINSTANCE instance_{nullptr};
  HWND hwnd_{nullptr};
  HICON icon_{nullptr};
  bool icon_added_{false};
  UINT taskbar_created_msg_{0};
  MessageFilter message_filter_;
  std::function<bool(bool)> automation_toggle_;
  bool automation_enabled_{false};
};

}  // namespace qingying
