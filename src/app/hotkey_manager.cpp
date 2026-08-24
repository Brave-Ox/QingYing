#include "qingying/app/hotkey_manager.hpp"

#include "qingying/app/app_messages.hpp"

namespace qingying {

HotkeyManager::~HotkeyManager() = default;

bool HotkeyManager::registerCaptureHotkey(HWND hwnd) {
  if (hwnd == nullptr || registered_) {
    return false;
  }

  hotkey_id_ = HotkeyIds::kCapture;
  if (!RegisterHotKey(hwnd, hotkey_id_, HotkeyDefaults::kCaptureModifiers,
                      HotkeyDefaults::kCaptureVirtualKey)) {
    return false;
  }

  registered_ = true;
  return true;
}

void HotkeyManager::unregisterAll(HWND hwnd) {
  if (!registered_ || hwnd == nullptr) {
    return;
  }
  UnregisterHotKey(hwnd, hotkey_id_);
  registered_ = false;
  hotkey_id_ = 0;
}

}  // namespace qingying
