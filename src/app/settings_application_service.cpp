#include "qingying/app/settings_application_service.hpp"

#include <optional>
#include <utility>

namespace qingying {
namespace {

class UserSettingsPreparedWritePort final : public SettingsPreparedWritePort
{
 public:
  explicit UserSettingsPreparedWritePort(PreparedSettingsWrite write)
      : m_write(std::move(write))
  {
  }

  SettingsStoreError commit() noexcept override
  {
    if (!m_write.has_value())
    {
      return SettingsStoreError::PreparedWriteInactive;
    }
    const SettingsStoreError result = m_write->commit();
    if (result == SettingsStoreError::None)
    {
      m_write.reset();
    }
    return result;
  }

  void cancel() noexcept override
  {
    if (m_write.has_value())
    {
      m_write->cancel();
      m_write.reset();
    }
  }

 private:
  std::optional<PreparedSettingsWrite> m_write;
};

class UserSettingsStorePort final : public SettingsStorePort
{
 public:
  explicit UserSettingsStorePort(UserSettingsStore& store) noexcept
      : m_store(store)
  {
  }

  UserSettingsLoadResult load() const noexcept override
  {
    return m_store.load();
  }

  SettingsStorePrepareResult prepareSave(
      const UserSettings& settings) override
  {
    SettingsPrepareResult result = m_store.prepareSave(settings);
    if (result.m_error != SettingsStoreError::None ||
        !result.m_write.has_value())
    {
      return SettingsStorePrepareResult{result.m_error, nullptr};
    }
    return SettingsStorePrepareResult{
        SettingsStoreError::None,
        std::make_unique<UserSettingsPreparedWritePort>(
            std::move(*result.m_write))};
  }

 private:
  UserSettingsStore& m_store;
};

bool selectionShortcutsEqual(const SelectionShortcutSettings& left,
                             const SelectionShortcutSettings& right) noexcept
{
  return left.m_copy == right.m_copy &&
         left.m_toggle_longshot == right.m_toggle_longshot;
}

bool shortcutsChanged(const SettingsDraft& draft) noexcept
{
  return draft.m_settings.m_capture_hotkey !=
             draft.m_baseline.m_settings.m_capture_hotkey ||
         !selectionShortcutsEqual(draft.m_settings.m_selection_shortcuts,
                                  draft.m_baseline.m_settings.m_selection_shortcuts);
}

void addErrorOnce(std::vector<SettingsTransactionError>& errors,
                  SettingsTransactionError error)
{
  for (const SettingsTransactionError existing : errors)
  {
    if (existing == error)
    {
      return;
    }
  }
  errors.push_back(error);
}

}  // namespace

bool SettingsFieldErrors::any() const noexcept
{
  return m_capture_hotkey != SettingsFieldError::None ||
         m_copy_shortcut != SettingsFieldError::None ||
         m_toggle_longshot_shortcut != SettingsFieldError::None ||
         m_longshot_limits != SettingsFieldError::None;
}

SettingsDraft makeSettingsDraft(const SettingsState& state) noexcept
{
  return SettingsDraft{state, state.m_settings, state.m_autostart_enabled,
                       state.m_agent_enabled};
}

SettingsApplicationService::SettingsApplicationService(
    SettingsStorePort& store, HotkeyManager& hotkeys,
    LongShotLimitsProvider& limits_provider, SettingsSystemPorts system_ports)
    : m_store(&store),
      m_hotkeys(hotkeys),
      m_limits_provider(limits_provider),
      m_system_ports(std::move(system_ports))
{
}

SettingsApplicationService::SettingsApplicationService(
    UserSettingsStore& store, HotkeyManager& hotkeys,
    LongShotLimitsProvider& limits_provider, SettingsSystemPorts system_ports)
    : m_owned_store(std::make_unique<UserSettingsStorePort>(store)),
      m_store(m_owned_store.get()),
      m_hotkeys(hotkeys),
      m_limits_provider(limits_provider),
      m_system_ports(std::move(system_ports))
{
}

SettingsState SettingsApplicationService::loadState() const
{
  const UserSettingsLoadResult settings_result = m_store->load();
  SettingsState state;
  state.m_settings = settings_result.m_settings;
  state.m_settings_store_error = settings_result.m_error;
  if (!hasSystemPorts())
  {
    return state;
  }
  state.m_autostart_enabled = m_system_ports.m_read_autostart();
  state.m_agent_enabled = m_system_ports.m_read_agent_enabled();
  state.m_capture_hotkey_available =
      m_system_ports.m_capture_hotkey_available(state.m_settings.m_capture_hotkey);
  return state;
}

SettingsApplyResult SettingsApplicationService::apply(
    const SettingsDraft& draft)
{
  SettingsApplyResult result;
  result.m_state = loadState();
  const UserSettingsValidation validation = validateUserSettings(draft.m_settings);
  if (!validation.m_capture_hotkey_valid)
  {
    result.m_field_errors.m_capture_hotkey =
        SettingsFieldError::CaptureHotkeyInvalid;
  }
  if (!validation.m_copy_shortcut_valid)
  {
    result.m_field_errors.m_copy_shortcut = SettingsFieldError::CopyShortcutInvalid;
  }
  if (!validation.m_toggle_longshot_shortcut_valid)
  {
    result.m_field_errors.m_toggle_longshot_shortcut =
        SettingsFieldError::ToggleLongShotShortcutInvalid;
  }
  if (!validation.m_shortcuts_unique)
  {
    if (result.m_field_errors.m_capture_hotkey == SettingsFieldError::None)
    {
      result.m_field_errors.m_capture_hotkey =
          SettingsFieldError::ShortcutsNotUnique;
    }
    if (result.m_field_errors.m_copy_shortcut == SettingsFieldError::None)
    {
      result.m_field_errors.m_copy_shortcut =
          SettingsFieldError::ShortcutsNotUnique;
    }
    if (result.m_field_errors.m_toggle_longshot_shortcut ==
        SettingsFieldError::None)
    {
      result.m_field_errors.m_toggle_longshot_shortcut =
          SettingsFieldError::ShortcutsNotUnique;
    }
  }
  if (!validation.m_longshot_limits_valid)
  {
    result.m_field_errors.m_longshot_limits =
        SettingsFieldError::LongShotLimitsInvalid;
  }
  if (result.m_field_errors.any())
  {
    return result;
  }
  if (!hasSystemPorts())
  {
    result.m_errors.push_back(SettingsTransactionError::SystemPortUnavailable);
    return failureResult(std::move(result));
  }

  const bool capture_hotkey_changed =
      draft.m_settings.m_capture_hotkey !=
      draft.m_baseline.m_settings.m_capture_hotkey;
  const bool copy_shortcut_changed =
      draft.m_settings.m_selection_shortcuts.m_copy !=
      draft.m_baseline.m_settings.m_selection_shortcuts.m_copy;
  const bool toggle_shortcut_changed =
      draft.m_settings.m_selection_shortcuts.m_toggle_longshot !=
      draft.m_baseline.m_settings.m_selection_shortcuts.m_toggle_longshot;
  const bool has_shortcut_changes = shortcutsChanged(draft);
  if (has_shortcut_changes && m_system_ports.m_workflow_active())
  {
    if (capture_hotkey_changed)
    {
      result.m_field_errors.m_capture_hotkey =
          SettingsFieldError::ShortcutChangesBlockedByActiveWorkflow;
    }
    if (copy_shortcut_changed)
    {
      result.m_field_errors.m_copy_shortcut =
          SettingsFieldError::ShortcutChangesBlockedByActiveWorkflow;
    }
    if (toggle_shortcut_changed)
    {
      result.m_field_errors.m_toggle_longshot_shortcut =
          SettingsFieldError::ShortcutChangesBlockedByActiveWorkflow;
    }
    return result;
  }

  std::optional<PreparedCaptureHotkey> prepared_hotkey;
  if (capture_hotkey_changed)
  {
    const HWND owner_window = m_system_ports.m_owner_window();
    if (owner_window == nullptr)
    {
      result.m_field_errors.m_capture_hotkey =
          SettingsFieldError::OwnerWindowUnavailable;
      return result;
    }
    prepared_hotkey.emplace(m_hotkeys.prepareCaptureHotkey(
        owner_window, draft.m_settings.m_capture_hotkey));
    if (!prepared_hotkey->valid())
    {
      result.m_field_errors.m_capture_hotkey =
          SettingsFieldError::CaptureHotkeyUnavailable;
      return result;
    }
  }

  if (has_shortcut_changes)
  {
    const ShortcutBinding binding_to_release = capture_hotkey_changed
        ? draft.m_baseline.m_settings.m_capture_hotkey
        : ShortcutBinding{};
    const SelectionShortcutSettings& shortcuts =
        draft.m_settings.m_selection_shortcuts;
    if (!shortcuts.m_copy.empty() &&
        !m_system_ports.m_selection_shortcut_available(
            shortcuts.m_copy, binding_to_release))
    {
      result.m_field_errors.m_copy_shortcut =
          SettingsFieldError::CopyShortcutUnavailable;
      return result;
    }
    if (!shortcuts.m_toggle_longshot.empty() &&
        !m_system_ports.m_selection_shortcut_available(
            shortcuts.m_toggle_longshot, binding_to_release))
    {
      result.m_field_errors.m_toggle_longshot_shortcut =
          SettingsFieldError::ToggleLongShotShortcutUnavailable;
      return result;
    }
  }

  SettingsStorePrepareResult prepare_result =
      m_store->prepareSave(draft.m_settings);
  if (prepare_result.m_error != SettingsStoreError::None ||
      !prepare_result.m_write)
  {
    result.m_errors.push_back(SettingsTransactionError::SettingsPrepareFailed);
    return failureResult(std::move(result));
  }
  std::unique_ptr<SettingsPreparedWritePort> prepared_settings =
      std::move(prepare_result.m_write);

  const bool autostart_changed =
      draft.m_autostart_enabled != draft.m_baseline.m_autostart_enabled;
  const bool agent_changed =
      draft.m_agent_enabled != draft.m_baseline.m_agent_enabled;
  bool autostart_applied = false;
  bool agent_applied = false;

  if (autostart_changed)
  {
    if (!m_system_ports.m_write_autostart(draft.m_autostart_enabled))
    {
      result.m_errors.push_back(SettingsTransactionError::AutostartApplyFailed);
      prepared_settings->cancel();
      return failureResult(std::move(result));
    }
    autostart_applied = true;
  }
  if (agent_changed)
  {
    if (!m_system_ports.m_write_agent_enabled(draft.m_agent_enabled))
    {
      result.m_errors.push_back(SettingsTransactionError::AgentApplyFailed);
      if (autostart_applied && !m_system_ports.m_write_autostart(
                                  draft.m_baseline.m_autostart_enabled))
      {
        addErrorOnce(result.m_rollback_errors,
                     SettingsTransactionError::AutostartRollbackFailed);
      }
      prepared_settings->cancel();
      return failureResult(std::move(result));
    }
    agent_applied = true;
  }

  const SettingsStoreError commit_error = prepared_settings->commit();
  if (commit_error != SettingsStoreError::None)
  {
    result.m_errors.push_back(SettingsTransactionError::SettingsCommitFailed);
    if (agent_applied && !m_system_ports.m_write_agent_enabled(
                             draft.m_baseline.m_agent_enabled))
    {
      addErrorOnce(result.m_rollback_errors,
                   SettingsTransactionError::AgentRollbackFailed);
    }
    if (autostart_applied && !m_system_ports.m_write_autostart(
                                draft.m_baseline.m_autostart_enabled))
    {
      addErrorOnce(result.m_rollback_errors,
                   SettingsTransactionError::AutostartRollbackFailed);
    }
    prepared_settings->cancel();
    return failureResult(std::move(result));
  }

  if (prepared_hotkey.has_value())
  {
    const HotkeyCommitResult hotkey_result = prepared_hotkey->commit();
    if (hotkey_result == HotkeyCommitResult::PreparedTokenInactive)
    {
      result.m_errors.push_back(SettingsTransactionError::HotkeyCommitFailed);
      return failureResult(std::move(result));
    }
    result.m_old_hotkey_release_pending =
        hotkey_result ==
        HotkeyCommitResult::CommittedWithOldBindingPendingRelease;
  }

  m_limits_provider.update(draft.m_settings.m_longshot_limits);
  result.m_committed = true;
  result.m_state = loadState();
  return result;
}

bool SettingsApplicationService::hasSystemPorts() const noexcept
{
  return static_cast<bool>(m_system_ports.m_owner_window) &&
         static_cast<bool>(m_system_ports.m_workflow_active) &&
         static_cast<bool>(m_system_ports.m_read_autostart) &&
         static_cast<bool>(m_system_ports.m_write_autostart) &&
         static_cast<bool>(m_system_ports.m_read_agent_enabled) &&
         static_cast<bool>(m_system_ports.m_write_agent_enabled) &&
         static_cast<bool>(m_system_ports.m_capture_hotkey_available) &&
         static_cast<bool>(m_system_ports.m_selection_shortcut_available);
}

SettingsApplyResult SettingsApplicationService::failureResult(
    SettingsApplyResult result) const
{
  result.m_state = loadState();
  return result;
}

}  // namespace qingying
