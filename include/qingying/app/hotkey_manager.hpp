#pragma once

#include <Windows.h>

namespace qingying {

// Global hotkey registration (RegisterHotKey) — lives in app only.
class HotkeyManager {
 public:
  HotkeyManager() = default;
  ~HotkeyManager();

  HotkeyManager(const HotkeyManager&) = delete;
  HotkeyManager& operator=(const HotkeyManager&) = delete;

  bool registerCaptureHotkey(HWND hwnd);
  void unregisterAll(HWND hwnd);

 private:
  bool registered_{false};
  int hotkey_id_{0};
};

}  // namespace qingying
