#pragma once

#include "qingying/ui/shortcut_types.hpp"

#include <Windows.h>

#include <functional>

namespace qingying {

struct HotkeyPlatformOperations
{
  std::function<bool(HWND, int, UINT, UINT)> m_register_hotkey;
  std::function<bool(HWND, int)> m_unregister_hotkey;
};

enum class HotkeyCommitResult
{
  Committed,
  CommittedWithOldBindingPendingRelease,
  PreparedTokenInactive,
};

class HotkeyManager;

class PreparedCaptureHotkey final
{
 public:
  PreparedCaptureHotkey() = delete;
  ~PreparedCaptureHotkey();

  PreparedCaptureHotkey(const PreparedCaptureHotkey&) = delete;
  PreparedCaptureHotkey& operator=(const PreparedCaptureHotkey&) = delete;
  PreparedCaptureHotkey(PreparedCaptureHotkey&& other) noexcept;
  PreparedCaptureHotkey& operator=(PreparedCaptureHotkey&& other) noexcept;

  bool valid() const noexcept;
  HotkeyCommitResult commit() noexcept;
  void cancel() noexcept;

 private:
  friend class HotkeyManager;

  PreparedCaptureHotkey(HotkeyManager* manager, HWND hwnd, int hotkey_id,
                        ShortcutBinding binding) noexcept;

  HotkeyManager* m_manager{nullptr};
  HWND m_hwnd{nullptr};
  int m_hotkey_id{0};
  ShortcutBinding m_binding;
};

// Global hotkey registration (RegisterHotKey) — lives in app only.
class HotkeyManager final
{
 public:
  HotkeyManager();
  explicit HotkeyManager(HotkeyPlatformOperations operations);
  ~HotkeyManager();

  HotkeyManager(const HotkeyManager&) = delete;
  HotkeyManager& operator=(const HotkeyManager&) = delete;

  bool registerCaptureHotkey(HWND hwnd, const ShortcutBinding& binding);
  PreparedCaptureHotkey prepareCaptureHotkey(
      HWND hwnd, const ShortcutBinding& binding);
  ShortcutBinding currentCaptureHotkey() const noexcept;
  bool isCurrentCaptureHotkeyId(int hotkey_id) const noexcept;
  void maintenance(HWND hwnd) noexcept;
  void unregisterAll(HWND hwnd);

 private:
  friend class PreparedCaptureHotkey;

  bool captureBindingIsValid(const ShortcutBinding& binding) const noexcept;
  int nextCaptureHotkeyId() const noexcept;
  bool retryPendingCleanup(HWND hwnd) noexcept;
  void cancelPreparedHotkey(HWND hwnd, int hotkey_id) noexcept;
  HotkeyCommitResult commitPreparedHotkey(HWND hwnd, int hotkey_id,
                                          const ShortcutBinding& binding) noexcept;

  HotkeyPlatformOperations m_operations;
  ShortcutBinding m_current_capture_hotkey;
  int m_current_hotkey_id{0};
  int m_prepared_hotkey_id{0};
  int m_pending_cleanup_hotkey_id{0};
};

}  // namespace qingying
