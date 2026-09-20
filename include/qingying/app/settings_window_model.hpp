#pragma once

#include <optional>
#include <string>

#include "qingying/app/settings_application_service.hpp"

namespace qingying {

enum class SettingsPage
{
  General,
  Hotkeys,
  LongShot,
};

enum class SettingsShortcutField
{
  Capture,
  Copy,
  ToggleLongShot,
};

enum class SettingsWindowField
{
  CaptureHotkey,
  CopyShortcut,
  ToggleLongShotShortcut,
  LongShotLimits,
};

std::wstring settingsFieldErrorMessage(SettingsFieldError error);
std::wstring settingsTransactionErrorMessage(SettingsTransactionError error);

class SettingsWindowModel final
{
 public:
  explicit SettingsWindowModel(const SettingsState& initial_state);

  SettingsPage currentPage() const noexcept;
  void selectPage(SettingsPage page) noexcept;
  void restoreCurrentPageDefaults() noexcept;

  bool beginShortcutRecording(SettingsShortcutField field) noexcept;
  void cancelShortcutRecording() noexcept;
  bool recordShortcut(const ShortcutBinding& binding) noexcept;
  bool clearShortcut(SettingsShortcutField field) noexcept;
  std::optional<SettingsShortcutField> recordingShortcut() const noexcept;

  void setAutostartEnabled(bool enabled) noexcept;
  void setAgentEnabled(bool enabled) noexcept;
  void setLongShotMaxFrames(int max_frames) noexcept;
  void setLongShotMaxOutputHeight(int max_output_height) noexcept;

  const SettingsDraft& draft() const noexcept;
  bool dirty() const noexcept;
  bool canApply() const noexcept;
  bool hasExternalChangeNotice() const noexcept;
  bool shouldWarnLongShotDuration() const noexcept;
  bool shouldWarnLongShotMemory() const noexcept;

  SettingsFieldError fieldError(SettingsWindowField field) const noexcept;
  std::wstring bannerMessage() const;
  void recordApplyResult(const SettingsApplyResult& result);
  void refreshExternalState(const SettingsState& state) noexcept;

 private:
  void clearApplyErrors() noexcept;

  SettingsState m_baseline;
  SettingsDraft m_draft;
  SettingsPage m_current_page{SettingsPage::General};
  std::optional<SettingsShortcutField> m_recording_shortcut;
  SettingsApplyResult m_last_apply_result;
  bool m_has_apply_result{false};
  bool m_has_external_change_notice{false};
};

}  // namespace qingying
