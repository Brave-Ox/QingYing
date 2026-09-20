#include "qingying/app/settings_window_model.hpp"

namespace qingying {
namespace {

constexpr int RecommendedLongShotFrames = 30;
constexpr int RecommendedLongShotOutputHeight = 30000;

bool selectionShortcutsEqual(const SelectionShortcutSettings& left,
                             const SelectionShortcutSettings& right) noexcept
{
  return left.m_copy == right.m_copy &&
         left.m_toggle_longshot == right.m_toggle_longshot;
}

bool settingsEqual(const UserSettings& left, const UserSettings& right) noexcept
{
  return left.m_schema_version == right.m_schema_version &&
         left.m_capture_hotkey == right.m_capture_hotkey &&
         selectionShortcutsEqual(left.m_selection_shortcuts,
                                 right.m_selection_shortcuts) &&
         left.m_longshot_limits.max_frames == right.m_longshot_limits.max_frames &&
         left.m_longshot_limits.max_output_height ==
             right.m_longshot_limits.max_output_height;
}

SettingsFieldError validationErrorForField(const SettingsDraft& draft,
                                           SettingsWindowField field) noexcept
{
  const UserSettingsValidation validation = validateUserSettings(draft.m_settings);
  switch (field)
  {
    case SettingsWindowField::CaptureHotkey:
      if (!validation.m_capture_hotkey_valid)
      {
        return SettingsFieldError::CaptureHotkeyInvalid;
      }
      return validation.m_shortcuts_unique
          ? SettingsFieldError::None
          : SettingsFieldError::ShortcutsNotUnique;
    case SettingsWindowField::CopyShortcut:
      if (!validation.m_copy_shortcut_valid)
      {
        return SettingsFieldError::CopyShortcutInvalid;
      }
      return validation.m_shortcuts_unique
          ? SettingsFieldError::None
          : SettingsFieldError::ShortcutsNotUnique;
    case SettingsWindowField::ToggleLongShotShortcut:
      if (!validation.m_toggle_longshot_shortcut_valid)
      {
        return SettingsFieldError::ToggleLongShotShortcutInvalid;
      }
      return validation.m_shortcuts_unique
          ? SettingsFieldError::None
          : SettingsFieldError::ShortcutsNotUnique;
    case SettingsWindowField::LongShotLimits:
      return validation.m_longshot_limits_valid
          ? SettingsFieldError::None
          : SettingsFieldError::LongShotLimitsInvalid;
  }
  return SettingsFieldError::None;
}

SettingsFieldError savedErrorForField(const SettingsFieldErrors& errors,
                                      SettingsWindowField field) noexcept
{
  switch (field)
  {
    case SettingsWindowField::CaptureHotkey:
      return errors.m_capture_hotkey;
    case SettingsWindowField::CopyShortcut:
      return errors.m_copy_shortcut;
    case SettingsWindowField::ToggleLongShotShortcut:
      return errors.m_toggle_longshot_shortcut;
    case SettingsWindowField::LongShotLimits:
      return errors.m_longshot_limits;
  }
  return SettingsFieldError::None;
}

}  // namespace

std::wstring settingsFieldErrorMessage(SettingsFieldError error)
{
  switch (error)
  {
    case SettingsFieldError::None:
      return L"";
    case SettingsFieldError::CaptureHotkeyInvalid:
      return L"截图快捷键必须包含有效的修饰键。";
    case SettingsFieldError::CopyShortcutInvalid:
      return L"复制快捷键格式无效。";
    case SettingsFieldError::ToggleLongShotShortcutInvalid:
      return L"长截图控制快捷键格式无效。";
    case SettingsFieldError::ShortcutsNotUnique:
      return L"三个快捷键不能使用相同组合。";
    case SettingsFieldError::LongShotLimitsInvalid:
      return L"长截图限制超出允许范围。";
    case SettingsFieldError::ShortcutChangesBlockedByActiveWorkflow:
      return L"请结束当前截图后应用快捷键。";
    case SettingsFieldError::OwnerWindowUnavailable:
      return L"设置窗口尚未准备好注册快捷键。";
    case SettingsFieldError::CaptureHotkeyUnavailable:
      return L"该截图快捷键已被其他程序占用。";
    case SettingsFieldError::CopyShortcutUnavailable:
      return L"该复制快捷键已被其他程序占用。";
    case SettingsFieldError::ToggleLongShotShortcutUnavailable:
      return L"该长截图快捷键已被其他程序占用。";
  }
  return L"设置字段无效。";
}

std::wstring settingsTransactionErrorMessage(SettingsTransactionError error)
{
  switch (error)
  {
    case SettingsTransactionError::SystemPortUnavailable:
      return L"设置服务尚未连接到系统功能。";
    case SettingsTransactionError::SettingsPrepareFailed:
      return L"无法准备保存设置。";
    case SettingsTransactionError::AutostartApplyFailed:
      return L"无法更新开机自启设置。";
    case SettingsTransactionError::AgentApplyFailed:
      return L"无法更新本机 Agent 接口。";
    case SettingsTransactionError::SettingsCommitFailed:
      return L"无法提交设置快照。";
    case SettingsTransactionError::HotkeyCommitFailed:
      return L"无法切换截图快捷键。";
    case SettingsTransactionError::AutostartRollbackFailed:
      return L"开机自启回滚失败，请检查当前状态。";
    case SettingsTransactionError::AgentRollbackFailed:
      return L"本机 Agent 接口回滚失败，请检查当前状态。";
  }
  return L"设置操作失败。";
}

SettingsWindowModel::SettingsWindowModel(const SettingsState& initial_state)
    : m_baseline(initial_state),
      m_draft(makeSettingsDraft(initial_state))
{
}

SettingsPage SettingsWindowModel::currentPage() const noexcept
{
  return m_current_page;
}

void SettingsWindowModel::selectPage(SettingsPage page) noexcept
{
  m_current_page = page;
  cancelShortcutRecording();
}

void SettingsWindowModel::restoreCurrentPageDefaults() noexcept
{
  const UserSettings defaults = defaultUserSettings();
  switch (m_current_page)
  {
    case SettingsPage::General:
      m_draft.m_autostart_enabled = false;
      m_draft.m_agent_enabled = false;
      break;
    case SettingsPage::Hotkeys:
      m_draft.m_settings.m_capture_hotkey = defaults.m_capture_hotkey;
      m_draft.m_settings.m_selection_shortcuts = defaults.m_selection_shortcuts;
      break;
    case SettingsPage::LongShot:
      m_draft.m_settings.m_longshot_limits = defaults.m_longshot_limits;
      break;
  }
  clearApplyErrors();
  cancelShortcutRecording();
}

bool SettingsWindowModel::beginShortcutRecording(
    SettingsShortcutField field) noexcept
{
  m_recording_shortcut = field;
  return true;
}

void SettingsWindowModel::cancelShortcutRecording() noexcept
{
  m_recording_shortcut.reset();
}

bool SettingsWindowModel::recordShortcut(const ShortcutBinding& binding) noexcept
{
  if (!m_recording_shortcut.has_value())
  {
    return false;
  }
  switch (*m_recording_shortcut)
  {
    case SettingsShortcutField::Capture:
      m_draft.m_settings.m_capture_hotkey = binding;
      break;
    case SettingsShortcutField::Copy:
      m_draft.m_settings.m_selection_shortcuts.m_copy = binding;
      break;
    case SettingsShortcutField::ToggleLongShot:
      m_draft.m_settings.m_selection_shortcuts.m_toggle_longshot = binding;
      break;
  }
  clearApplyErrors();
  cancelShortcutRecording();
  return true;
}

bool SettingsWindowModel::clearShortcut(SettingsShortcutField field) noexcept
{
  if (field == SettingsShortcutField::Capture)
  {
    return false;
  }
  if (field == SettingsShortcutField::Copy)
  {
    m_draft.m_settings.m_selection_shortcuts.m_copy = ShortcutBinding{};
  }
  else
  {
    m_draft.m_settings.m_selection_shortcuts.m_toggle_longshot =
        ShortcutBinding{};
  }
  clearApplyErrors();
  return true;
}

std::optional<SettingsShortcutField>
SettingsWindowModel::recordingShortcut() const noexcept
{
  return m_recording_shortcut;
}

void SettingsWindowModel::setAutostartEnabled(bool enabled) noexcept
{
  m_draft.m_autostart_enabled = enabled;
  clearApplyErrors();
}

void SettingsWindowModel::setAgentEnabled(bool enabled) noexcept
{
  m_draft.m_agent_enabled = enabled;
  clearApplyErrors();
}

void SettingsWindowModel::setLongShotMaxFrames(int max_frames) noexcept
{
  m_draft.m_settings.m_longshot_limits.max_frames = max_frames;
  clearApplyErrors();
}

void SettingsWindowModel::setLongShotMaxOutputHeight(
    int max_output_height) noexcept
{
  m_draft.m_settings.m_longshot_limits.max_output_height = max_output_height;
  clearApplyErrors();
}

const SettingsDraft& SettingsWindowModel::draft() const noexcept
{
  return m_draft;
}

bool SettingsWindowModel::dirty() const noexcept
{
  return !settingsEqual(m_draft.m_settings, m_baseline.m_settings) ||
         m_draft.m_autostart_enabled != m_baseline.m_autostart_enabled ||
         m_draft.m_agent_enabled != m_baseline.m_agent_enabled;
}

bool SettingsWindowModel::canApply() const noexcept
{
  return dirty() && !m_recording_shortcut.has_value() &&
         validationErrorForField(m_draft,
                                 SettingsWindowField::CaptureHotkey) ==
             SettingsFieldError::None &&
         validationErrorForField(m_draft, SettingsWindowField::CopyShortcut) ==
             SettingsFieldError::None &&
         validationErrorForField(
             m_draft, SettingsWindowField::ToggleLongShotShortcut) ==
             SettingsFieldError::None &&
         validationErrorForField(m_draft, SettingsWindowField::LongShotLimits) ==
             SettingsFieldError::None;
}

bool SettingsWindowModel::hasExternalChangeNotice() const noexcept
{
  return m_has_external_change_notice;
}

bool SettingsWindowModel::shouldWarnLongShotDuration() const noexcept
{
  return m_draft.m_settings.m_longshot_limits.max_frames >
         RecommendedLongShotFrames;
}

bool SettingsWindowModel::shouldWarnLongShotMemory() const noexcept
{
  return m_draft.m_settings.m_longshot_limits.max_output_height >
         RecommendedLongShotOutputHeight;
}

SettingsFieldError SettingsWindowModel::fieldError(
    SettingsWindowField field) const noexcept
{
  const SettingsFieldError validation_error =
      validationErrorForField(m_draft, field);
  if (validation_error != SettingsFieldError::None)
  {
    return validation_error;
  }
  return m_has_apply_result
      ? savedErrorForField(m_last_apply_result.m_field_errors, field)
      : SettingsFieldError::None;
}

std::wstring SettingsWindowModel::bannerMessage() const
{
  if (m_has_apply_result)
  {
    if (!m_last_apply_result.m_errors.empty())
    {
      return settingsTransactionErrorMessage(m_last_apply_result.m_errors.front());
    }
    if (!m_last_apply_result.m_rollback_errors.empty())
    {
      return settingsTransactionErrorMessage(
          m_last_apply_result.m_rollback_errors.front());
    }
  }
  return m_has_external_change_notice
      ? L"系统状态已在外部更改，应用将使用当前选择。"
      : L"";
}

void SettingsWindowModel::recordApplyResult(const SettingsApplyResult& result)
{
  if (result.m_committed)
  {
    m_baseline = result.m_state;
    m_draft = makeSettingsDraft(result.m_state);
    m_has_apply_result = false;
    m_has_external_change_notice = false;
    cancelShortcutRecording();
    return;
  }
  m_last_apply_result = result;
  m_has_apply_result = true;
}

void SettingsWindowModel::refreshExternalState(const SettingsState& state) noexcept
{
  const SettingsState previous_baseline = m_baseline;
  if (m_draft.m_autostart_enabled == previous_baseline.m_autostart_enabled)
  {
    m_draft.m_autostart_enabled = state.m_autostart_enabled;
  }
  else if (state.m_autostart_enabled != previous_baseline.m_autostart_enabled)
  {
    m_has_external_change_notice = true;
  }
  if (m_draft.m_agent_enabled == previous_baseline.m_agent_enabled)
  {
    m_draft.m_agent_enabled = state.m_agent_enabled;
  }
  else if (state.m_agent_enabled != previous_baseline.m_agent_enabled)
  {
    m_has_external_change_notice = true;
  }

  if (m_draft.m_settings.m_capture_hotkey ==
      previous_baseline.m_settings.m_capture_hotkey)
  {
    m_draft.m_settings.m_capture_hotkey = state.m_settings.m_capture_hotkey;
  }
  else if (state.m_settings.m_capture_hotkey !=
           previous_baseline.m_settings.m_capture_hotkey)
  {
    m_has_external_change_notice = true;
  }
  if (m_draft.m_settings.m_selection_shortcuts.m_copy ==
      previous_baseline.m_settings.m_selection_shortcuts.m_copy)
  {
    m_draft.m_settings.m_selection_shortcuts.m_copy =
        state.m_settings.m_selection_shortcuts.m_copy;
  }
  else if (state.m_settings.m_selection_shortcuts.m_copy !=
           previous_baseline.m_settings.m_selection_shortcuts.m_copy)
  {
    m_has_external_change_notice = true;
  }
  if (m_draft.m_settings.m_selection_shortcuts.m_toggle_longshot ==
      previous_baseline.m_settings.m_selection_shortcuts.m_toggle_longshot)
  {
    m_draft.m_settings.m_selection_shortcuts.m_toggle_longshot =
        state.m_settings.m_selection_shortcuts.m_toggle_longshot;
  }
  else if (state.m_settings.m_selection_shortcuts.m_toggle_longshot !=
           previous_baseline.m_settings.m_selection_shortcuts.m_toggle_longshot)
  {
    m_has_external_change_notice = true;
  }
  if (m_draft.m_settings.m_longshot_limits.max_frames ==
          previous_baseline.m_settings.m_longshot_limits.max_frames &&
      m_draft.m_settings.m_longshot_limits.max_output_height ==
          previous_baseline.m_settings.m_longshot_limits.max_output_height)
  {
    m_draft.m_settings.m_longshot_limits = state.m_settings.m_longshot_limits;
  }
  else if (state.m_settings.m_longshot_limits.max_frames !=
               previous_baseline.m_settings.m_longshot_limits.max_frames ||
           state.m_settings.m_longshot_limits.max_output_height !=
               previous_baseline.m_settings.m_longshot_limits.max_output_height)
  {
    m_has_external_change_notice = true;
  }
  m_draft.m_settings.m_schema_version = state.m_settings.m_schema_version;
  m_baseline = state;
  m_draft.m_baseline = state;
  clearApplyErrors();
}

void SettingsWindowModel::clearApplyErrors() noexcept
{
  m_last_apply_result = SettingsApplyResult{};
  m_has_apply_result = false;
}

}  // namespace qingying
