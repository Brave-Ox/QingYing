#include <array>

#include <gtest/gtest.h>

#include "qingying/app/settings_window_model.hpp"

namespace qingying {
namespace {

SettingsState makeState()
{
  SettingsState state;
  state.m_settings = defaultUserSettings();
  return state;
}

TEST(SettingsWindowModelTest, StartsOnGeneralPageWithCleanDraft)
{
  SettingsWindowModel model(makeState());

  EXPECT_EQ(model.currentPage(), SettingsPage::General);
  EXPECT_FALSE(model.dirty());
  EXPECT_FALSE(model.canApply());
  EXPECT_FALSE(model.draft().m_autostart_enabled);
  EXPECT_FALSE(model.draft().m_agent_enabled);
}

TEST(SettingsWindowModelTest, RestoringCurrentPageChangesOnlyThatPageDraft)
{
  SettingsWindowModel model(makeState());
  model.setAutostartEnabled(true);
  model.setAgentEnabled(true);
  model.selectPage(SettingsPage::LongShot);
  model.setLongShotMaxFrames(45);

  model.restoreCurrentPageDefaults();

  EXPECT_EQ(model.draft().m_settings.m_longshot_limits.max_frames, 30);
  EXPECT_TRUE(model.draft().m_autostart_enabled);
  EXPECT_TRUE(model.draft().m_agent_enabled);

  model.selectPage(SettingsPage::General);
  model.restoreCurrentPageDefaults();

  EXPECT_FALSE(model.draft().m_autostart_enabled);
  EXPECT_FALSE(model.draft().m_agent_enabled);
}

TEST(SettingsWindowModelTest, ShortcutRecordingBlocksApplyUntilCancelled)
{
  SettingsWindowModel model(makeState());
  model.setLongShotMaxFrames(35);

  ASSERT_TRUE(model.beginShortcutRecording(SettingsShortcutField::Copy));
  EXPECT_TRUE(model.recordingShortcut().has_value());
  EXPECT_FALSE(model.canApply());

  model.cancelShortcutRecording();

  EXPECT_FALSE(model.recordingShortcut().has_value());
  EXPECT_TRUE(model.canApply());
}

TEST(SettingsWindowModelTest, RecordedShortcutUpdatesDraftAndEndsRecording)
{
  SettingsWindowModel model(makeState());
  ASSERT_TRUE(model.beginShortcutRecording(SettingsShortcutField::Copy));

  ASSERT_TRUE(model.recordShortcut(
      ShortcutBinding{MOD_CONTROL | MOD_ALT, static_cast<UINT>('X')}));

  EXPECT_FALSE(model.recordingShortcut().has_value());
  EXPECT_EQ(model.draft().m_settings.m_selection_shortcuts.m_copy,
            (ShortcutBinding{MOD_CONTROL | MOD_ALT, static_cast<UINT>('X')}));
  EXPECT_TRUE(model.canApply());
}

TEST(SettingsWindowModelTest, InvalidLongShotInputShowsFieldErrorAndDisablesApply)
{
  SettingsWindowModel model(makeState());
  model.setLongShotMaxFrames(1);

  EXPECT_EQ(model.fieldError(SettingsWindowField::LongShotLimits),
            SettingsFieldError::LongShotLimitsInvalid);
  EXPECT_FALSE(model.canApply());
}

TEST(SettingsWindowModelTest, InvalidCopyShortcutKeepsErrorOnCopyField)
{
  SettingsWindowModel model(makeState());
  ASSERT_TRUE(model.beginShortcutRecording(SettingsShortcutField::Copy));
  ASSERT_TRUE(model.recordShortcut(ShortcutBinding{MOD_CONTROL, 0}));

  EXPECT_EQ(model.fieldError(SettingsWindowField::CaptureHotkey),
            SettingsFieldError::None);
  EXPECT_EQ(model.fieldError(SettingsWindowField::CopyShortcut),
            SettingsFieldError::CopyShortcutInvalid);
  EXPECT_FALSE(model.canApply());
}

TEST(SettingsWindowModelTest, ExternalRefreshUpdatesUnmodifiedFields)
{
  SettingsWindowModel model(makeState());
  SettingsState external = makeState();
  external.m_autostart_enabled = true;
  external.m_agent_enabled = true;
  external.m_settings.m_longshot_limits.max_frames = 40;

  model.refreshExternalState(external);

  EXPECT_TRUE(model.draft().m_autostart_enabled);
  EXPECT_TRUE(model.draft().m_agent_enabled);
  EXPECT_EQ(model.draft().m_settings.m_longshot_limits.max_frames, 40);
  EXPECT_FALSE(model.hasExternalChangeNotice());
  EXPECT_FALSE(model.dirty());
}

TEST(SettingsWindowModelTest,
     ExternalRefreshPreservesModifiedFieldAndUpdatesItsBaseline)
{
  SettingsWindowModel model(makeState());
  model.setLongShotMaxFrames(35);
  SettingsState external = makeState();
  external.m_agent_enabled = true;
  external.m_settings.m_longshot_limits.max_frames = 40;

  model.refreshExternalState(external);

  EXPECT_EQ(model.draft().m_settings.m_longshot_limits.max_frames, 35);
  EXPECT_EQ(model.draft().m_baseline.m_settings.m_longshot_limits.max_frames,
            40);
  EXPECT_TRUE(model.draft().m_agent_enabled);
  EXPECT_TRUE(model.hasExternalChangeNotice());
  EXPECT_TRUE(model.dirty());
}

TEST(SettingsWindowModelTest, SuccessfulApplyResetsBaselineAndClearsErrors)
{
  SettingsWindowModel model(makeState());
  model.setLongShotMaxFrames(35);
  SettingsApplyResult failure;
  failure.m_field_errors.m_longshot_limits =
      SettingsFieldError::LongShotLimitsInvalid;
  model.recordApplyResult(failure);

  ASSERT_NE(model.fieldError(SettingsWindowField::LongShotLimits),
            SettingsFieldError::None);

  SettingsState applied = makeState();
  applied.m_settings.m_longshot_limits.max_frames = 35;
  SettingsApplyResult success;
  success.m_committed = true;
  success.m_state = applied;
  model.recordApplyResult(success);

  EXPECT_FALSE(model.dirty());
  EXPECT_EQ(model.fieldError(SettingsWindowField::LongShotLimits),
            SettingsFieldError::None);
  EXPECT_EQ(model.draft().m_settings.m_longshot_limits.max_frames, 35);
}

TEST(SettingsWindowModelTest, StableErrorsMapToChineseFieldOrBannerText)
{
  constexpr std::array<SettingsFieldError, 10> FieldErrors{
      SettingsFieldError::CaptureHotkeyInvalid,
      SettingsFieldError::CopyShortcutInvalid,
      SettingsFieldError::ToggleLongShotShortcutInvalid,
      SettingsFieldError::ShortcutsNotUnique,
      SettingsFieldError::LongShotLimitsInvalid,
      SettingsFieldError::ShortcutChangesBlockedByActiveWorkflow,
      SettingsFieldError::OwnerWindowUnavailable,
      SettingsFieldError::CaptureHotkeyUnavailable,
      SettingsFieldError::CopyShortcutUnavailable,
      SettingsFieldError::ToggleLongShotShortcutUnavailable};
  constexpr std::array<SettingsTransactionError, 8> TransactionErrors{
      SettingsTransactionError::SystemPortUnavailable,
      SettingsTransactionError::SettingsPrepareFailed,
      SettingsTransactionError::AutostartApplyFailed,
      SettingsTransactionError::AgentApplyFailed,
      SettingsTransactionError::SettingsCommitFailed,
      SettingsTransactionError::HotkeyCommitFailed,
      SettingsTransactionError::AutostartRollbackFailed,
      SettingsTransactionError::AgentRollbackFailed};

  for (const SettingsFieldError error : FieldErrors)
  {
    EXPECT_FALSE(settingsFieldErrorMessage(error).empty());
  }
  for (const SettingsTransactionError error : TransactionErrors)
  {
    EXPECT_FALSE(settingsTransactionErrorMessage(error).empty());
  }
}

}  // namespace
}  // namespace qingying
