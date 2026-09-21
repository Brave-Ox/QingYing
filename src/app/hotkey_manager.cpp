#include "qingying/app/hotkey_manager.hpp"

#include "qingying/app/app_messages.hpp"

#include <utility>

namespace qingying {
namespace {

constexpr int NoHotkeyId = 0;

bool registerWindowsHotkey(HWND hwnd, int hotkey_id, UINT modifiers,
                           UINT virtual_key)
{
  return RegisterHotKey(hwnd, hotkey_id, modifiers, virtual_key) != FALSE;
}

bool unregisterWindowsHotkey(HWND hwnd, int hotkey_id)
{
  return UnregisterHotKey(hwnd, hotkey_id) != FALSE;
}

HotkeyPlatformOperations defaultPlatformOperations()
{
  return HotkeyPlatformOperations{registerWindowsHotkey, unregisterWindowsHotkey};
}

}  // namespace

PreparedCaptureHotkey::PreparedCaptureHotkey(HotkeyManager* manager, HWND hwnd,
                                             int hotkey_id,
                                             ShortcutBinding binding) noexcept
    : m_manager(manager), m_hwnd(hwnd), m_hotkey_id(hotkey_id),
      m_binding(binding)
{
}

PreparedCaptureHotkey::~PreparedCaptureHotkey()
{
  cancel();
}

PreparedCaptureHotkey::PreparedCaptureHotkey(
    PreparedCaptureHotkey&& other) noexcept
    : m_manager(other.m_manager), m_hwnd(other.m_hwnd),
      m_hotkey_id(other.m_hotkey_id), m_binding(other.m_binding)
{
  other.m_manager = nullptr;
  other.m_hwnd = nullptr;
  other.m_hotkey_id = NoHotkeyId;
}

PreparedCaptureHotkey& PreparedCaptureHotkey::operator=(
    PreparedCaptureHotkey&& other) noexcept
{
  if (this != &other)
  {
    cancel();
    m_manager = other.m_manager;
    m_hwnd = other.m_hwnd;
    m_hotkey_id = other.m_hotkey_id;
    m_binding = other.m_binding;
    other.m_manager = nullptr;
    other.m_hwnd = nullptr;
    other.m_hotkey_id = NoHotkeyId;
  }
  return *this;
}

bool PreparedCaptureHotkey::valid() const noexcept
{
  return m_manager != nullptr && m_hwnd != nullptr &&
         m_hotkey_id != NoHotkeyId;
}

HotkeyCommitResult PreparedCaptureHotkey::commit() noexcept
{
  if (!valid())
  {
    return HotkeyCommitResult::PreparedTokenInactive;
  }

  const HotkeyCommitResult result =
      m_manager->commitPreparedHotkey(m_hwnd, m_hotkey_id, m_binding);
  if (result != HotkeyCommitResult::PreparedTokenInactive)
  {
    m_manager = nullptr;
    m_hwnd = nullptr;
    m_hotkey_id = NoHotkeyId;
  }
  return result;
}

void PreparedCaptureHotkey::cancel() noexcept
{
  if (valid())
  {
    m_manager->cancelPreparedHotkey(m_hwnd, m_hotkey_id);
  }
  m_manager = nullptr;
  m_hwnd = nullptr;
  m_hotkey_id = NoHotkeyId;
}

HotkeyManager::HotkeyManager()
    : HotkeyManager(defaultPlatformOperations())
{
}

HotkeyManager::HotkeyManager(HotkeyPlatformOperations operations)
    : m_operations(std::move(operations))
{
  if (!m_operations.m_register_hotkey || !m_operations.m_unregister_hotkey)
  {
    m_operations = defaultPlatformOperations();
  }
}

HotkeyManager::~HotkeyManager() = default;

bool HotkeyManager::registerCaptureHotkey(HWND hwnd,
                                          const ShortcutBinding& binding)
{
  if (hwnd == nullptr || m_current_hotkey_id != NoHotkeyId ||
      m_prepared_hotkey_id != NoHotkeyId || !bindingIsValid(binding) ||
      !retryPendingCleanup(hwnd))
  {
    return false;
  }

  const int hotkey_id = HotkeyIds::kCapturePrimary;
  if (!m_operations.m_register_hotkey(
          hwnd, hotkey_id, binding.m_modifiers | MOD_NOREPEAT,
          binding.m_virtual_key))
  {
    return false;
  }

  m_current_capture_hotkey = binding;
  m_current_hotkey_id = hotkey_id;
  return true;
}

PreparedCaptureHotkey HotkeyManager::prepareCaptureHotkey(
    HWND hwnd, const ShortcutBinding& binding)
{
  if (hwnd == nullptr || m_current_hotkey_id == NoHotkeyId ||
      m_prepared_hotkey_id != NoHotkeyId || !bindingIsValid(binding) ||
      !retryPendingCleanup(hwnd))
  {
    return PreparedCaptureHotkey(this, hwnd, NoHotkeyId, ShortcutBinding{});
  }

  const int hotkey_id = nextCaptureHotkeyId();
  if (!m_operations.m_register_hotkey(
          hwnd, hotkey_id, binding.m_modifiers | MOD_NOREPEAT,
          binding.m_virtual_key))
  {
    return PreparedCaptureHotkey(this, hwnd, NoHotkeyId, ShortcutBinding{});
  }

  m_prepared_hotkey_id = hotkey_id;
  return PreparedCaptureHotkey(this, hwnd, hotkey_id, binding);
}

ShortcutBinding HotkeyManager::currentCaptureHotkey() const noexcept
{
  return m_current_capture_hotkey;
}

bool HotkeyManager::isCurrentCaptureHotkeyId(int hotkey_id) const noexcept
{
  return m_current_hotkey_id != NoHotkeyId &&
         hotkey_id == m_current_hotkey_id;
}

bool HotkeyManager::registerCommandHotkey(HWND hwnd,
                                          const ShortcutBinding& binding)
{
  if (hwnd == nullptr || m_command_hotkey_id != NoHotkeyId ||
      !bindingIsValid(binding) || !retryPendingCleanup(hwnd))
  {
    return false;
  }

  if (!m_operations.m_register_hotkey(
          hwnd, HotkeyIds::kCommand, binding.m_modifiers | MOD_NOREPEAT,
          binding.m_virtual_key))
  {
    return false;
  }

  m_current_command_hotkey = binding;
  m_command_hotkey_id = HotkeyIds::kCommand;
  return true;
}

ShortcutBinding HotkeyManager::currentCommandHotkey() const noexcept
{
  return m_current_command_hotkey;
}

bool HotkeyManager::isCurrentCommandHotkeyId(int hotkey_id) const noexcept
{
  return m_command_hotkey_id != NoHotkeyId &&
         hotkey_id == m_command_hotkey_id;
}

bool HotkeyManager::isCaptureBindingAvailable(HWND hwnd,
                                              const ShortcutBinding& binding)
{
  if (binding == m_current_capture_hotkey)
  {
    return true;
  }
  if (hwnd == nullptr || !bindingIsValid(binding) ||
      !retryPendingCleanup(hwnd))
  {
    return false;
  }

  if (!m_operations.m_register_hotkey(
          hwnd, HotkeyIds::kAvailabilityProbe,
          binding.m_modifiers | MOD_NOREPEAT, binding.m_virtual_key))
  {
    return false;
  }
  return releaseHotkey(hwnd, HotkeyIds::kAvailabilityProbe);
}

void HotkeyManager::maintenance(HWND hwnd) noexcept
{
  if (hwnd != nullptr)
  {
    static_cast<void>(retryPendingCleanup(hwnd));
  }
}

void HotkeyManager::unregisterAll(HWND hwnd)
{
  if (hwnd == nullptr)
  {
    return;
  }

  if (m_prepared_hotkey_id != NoHotkeyId)
  {
    cancelPreparedHotkey(hwnd, m_prepared_hotkey_id);
  }
  static_cast<void>(retryPendingCleanup(hwnd));

  if (m_current_hotkey_id != NoHotkeyId)
  {
    static_cast<void>(releaseHotkey(hwnd, m_current_hotkey_id));
    m_current_hotkey_id = NoHotkeyId;
    m_current_capture_hotkey = ShortcutBinding{};
  }
  if (m_command_hotkey_id != NoHotkeyId)
  {
    static_cast<void>(releaseHotkey(hwnd, m_command_hotkey_id));
    m_command_hotkey_id = NoHotkeyId;
    m_current_command_hotkey = ShortcutBinding{};
  }
  static_cast<void>(retryPendingCleanup(hwnd));
}

bool HotkeyManager::bindingIsValid(
    const ShortcutBinding& binding) const noexcept
{
  return !binding.empty() && binding.m_modifiers != 0 && binding.valid() &&
         !shortcutIsReserved(binding);
}

int HotkeyManager::nextCaptureHotkeyId() const noexcept
{
  return m_current_hotkey_id == HotkeyIds::kCapturePrimary
             ? HotkeyIds::kCaptureSecondary
             : HotkeyIds::kCapturePrimary;
}

bool HotkeyManager::retryPendingCleanup(HWND hwnd) noexcept
{
  bool released_all = true;
  for (int& hotkey_id : m_pending_cleanup_hotkey_ids)
  {
    if (hotkey_id == NoHotkeyId)
    {
      continue;
    }
    if (m_operations.m_unregister_hotkey(hwnd, hotkey_id))
    {
      hotkey_id = NoHotkeyId;
    }
    else
    {
      released_all = false;
    }
  }
  return released_all;
}

bool HotkeyManager::releaseHotkey(HWND hwnd, int hotkey_id) noexcept
{
  if (hotkey_id == NoHotkeyId)
  {
    return true;
  }
  if (m_operations.m_unregister_hotkey(hwnd, hotkey_id))
  {
    return true;
  }
  rememberPendingCleanup(hotkey_id);
  return false;
}

void HotkeyManager::rememberPendingCleanup(int hotkey_id) noexcept
{
  if (hotkey_id == NoHotkeyId)
  {
    return;
  }
  for (const int pending_hotkey_id : m_pending_cleanup_hotkey_ids)
  {
    if (pending_hotkey_id == hotkey_id)
    {
      return;
    }
  }
  for (int& pending_hotkey_id : m_pending_cleanup_hotkey_ids)
  {
    if (pending_hotkey_id == NoHotkeyId)
    {
      pending_hotkey_id = hotkey_id;
      return;
    }
  }
}

void HotkeyManager::cancelPreparedHotkey(HWND hwnd, int hotkey_id) noexcept
{
  if (m_prepared_hotkey_id != hotkey_id || hotkey_id == NoHotkeyId)
  {
    return;
  }

  m_prepared_hotkey_id = NoHotkeyId;
  static_cast<void>(releaseHotkey(hwnd, hotkey_id));
}

HotkeyCommitResult HotkeyManager::commitPreparedHotkey(
    HWND hwnd, int hotkey_id, const ShortcutBinding& binding) noexcept
{
  if (m_prepared_hotkey_id != hotkey_id || hotkey_id == NoHotkeyId)
  {
    return HotkeyCommitResult::PreparedTokenInactive;
  }

  const int old_hotkey_id = m_current_hotkey_id;
  m_current_hotkey_id = hotkey_id;
  m_current_capture_hotkey = binding;
  m_prepared_hotkey_id = NoHotkeyId;
  if (releaseHotkey(hwnd, old_hotkey_id))
  {
    return HotkeyCommitResult::Committed;
  }

  return HotkeyCommitResult::CommittedWithOldBindingPendingRelease;
}

}  // namespace qingying
