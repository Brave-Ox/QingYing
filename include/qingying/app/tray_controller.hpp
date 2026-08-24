#pragma once

#include <Windows.h>

namespace qingying {

class TrayController {
 public:
  TrayController();
  ~TrayController();

  TrayController(const TrayController&) = delete;
  TrayController& operator=(const TrayController&) = delete;

  bool create(HINSTANCE instance);
  void destroy();

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
};

}  // namespace qingying
