#pragma once

#include "qingying/app/hotkey_manager.hpp"
#include "qingying/app/longshot_limits_provider.hpp"
#include "qingying/app/user_settings_store.hpp"

#include <Windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace qingying {

enum class SettingsFieldError : std::uint8_t
{
  None,
  CaptureHotkeyInvalid,
  CopyShortcutInvalid,
  ToggleLongShotShortcutInvalid,
  ShortcutsNotUnique,
  LongShotLimitsInvalid,
  ShortcutChangesBlockedByActiveWorkflow,
  OwnerWindowUnavailable,
  CaptureHotkeyUnavailable,
  CopyShortcutUnavailable,
  ToggleLongShotShortcutUnavailable,
};

enum class SettingsTransactionError : std::uint8_t
{
  SystemPortUnavailable,
  SettingsPrepareFailed,
  AutostartApplyFailed,
  AgentApplyFailed,
  SettingsCommitFailed,
  HotkeyCommitFailed,
  AutostartRollbackFailed,
  AgentRollbackFailed,
};

struct SettingsFieldErrors
{
  SettingsFieldError m_capture_hotkey{SettingsFieldError::None};
  SettingsFieldError m_copy_shortcut{SettingsFieldError::None};
  SettingsFieldError m_toggle_longshot_shortcut{SettingsFieldError::None};
  SettingsFieldError m_longshot_limits{SettingsFieldError::None};

  bool any() const noexcept;
};

struct SettingsState
{
  UserSettings m_settings{defaultUserSettings()};
  bool m_autostart_enabled{false};
  bool m_agent_enabled{false};
  bool m_capture_hotkey_available{false};
  SettingsStoreError m_settings_store_error{SettingsStoreError::None};
};

struct SettingsDraft
{
  SettingsState m_baseline;
  UserSettings m_settings{defaultUserSettings()};
  bool m_autostart_enabled{false};
  bool m_agent_enabled{false};
};

SettingsDraft makeSettingsDraft(const SettingsState& state) noexcept;

struct SettingsApplyResult
{
  bool m_committed{false};
  bool m_old_hotkey_release_pending{false};
  SettingsFieldErrors m_field_errors;
  std::vector<SettingsTransactionError> m_errors;
  std::vector<SettingsTransactionError> m_rollback_errors;
  SettingsState m_state;
};

class SettingsPreparedWritePort
{
 public:
  virtual ~SettingsPreparedWritePort() = default;

  virtual SettingsStoreError commit() noexcept = 0;
  virtual void cancel() noexcept = 0;
};

struct SettingsStorePrepareResult
{
  SettingsStoreError m_error{SettingsStoreError::None};
  std::unique_ptr<SettingsPreparedWritePort> m_write;
};

class SettingsStorePort
{
 public:
  virtual ~SettingsStorePort() = default;

  virtual UserSettingsLoadResult load() const noexcept = 0;
  virtual SettingsStorePrepareResult prepareSave(
      const UserSettings& settings) = 0;
};

struct SettingsSystemPorts
{
  std::function<HWND()> m_owner_window;
  std::function<bool()> m_workflow_active;
  std::function<bool()> m_read_autostart;
  std::function<bool(bool)> m_write_autostart;
  std::function<bool()> m_read_agent_enabled;
  std::function<bool(bool)> m_write_agent_enabled;
  std::function<bool(const ShortcutBinding&)> m_capture_hotkey_available;
  std::function<bool(const ShortcutBinding&, const ShortcutBinding&)>
      m_selection_shortcut_available;
};

class SettingsApplicationService final
{
 public:
  SettingsApplicationService(SettingsStorePort& store, HotkeyManager& hotkeys,
                             LongShotLimitsProvider& limits_provider,
                             SettingsSystemPorts system_ports);
  SettingsApplicationService(UserSettingsStore& store, HotkeyManager& hotkeys,
                             LongShotLimitsProvider& limits_provider,
                             SettingsSystemPorts system_ports);

  SettingsApplicationService(const SettingsApplicationService&) = delete;
  SettingsApplicationService& operator=(const SettingsApplicationService&) =
      delete;

  SettingsState loadState() const;
  SettingsApplyResult apply(const SettingsDraft& draft);

 private:
  bool hasSystemPorts() const noexcept;
  SettingsApplyResult failureResult(
      SettingsApplyResult result) const;

  std::unique_ptr<SettingsStorePort> m_owned_store;
  SettingsStorePort* m_store{nullptr};
  HotkeyManager& m_hotkeys;
  LongShotLimitsProvider& m_limits_provider;
  SettingsSystemPorts m_system_ports;
};

}  // namespace qingying
