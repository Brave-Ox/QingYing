#pragma once

#include <Windows.h>

#include <functional>
#include <string>
#include <utility>

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
  void setAutostartToggle(std::function<bool(bool)> toggle) { autostart_toggle_ = std::move(toggle); }
  void setAutostartEnabled(bool enabled) { autostart_enabled_ = enabled; }
  void setBeginCaptureCallback(std::function<void()> callback) { begin_capture_callback_ = std::move(callback); }
  void setSettingsCallback(std::function<void()> callback) { settings_callback_ = std::move(callback); }
  void setCaptureHotkeyDisplay(std::wstring display) { capture_hotkey_display_ = std::move(display); }
  const std::wstring& captureHotkeyDisplay() const noexcept { return capture_hotkey_display_; }

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
  std::function<void()> begin_capture_callback_;
  std::function<void()> settings_callback_;
  std::function<bool(bool)> autostart_toggle_;
  std::function<bool(bool)> automation_toggle_;
  std::wstring capture_hotkey_display_;
  bool autostart_enabled_{false};
  bool automation_enabled_{false};
};

}  // namespace qingying
