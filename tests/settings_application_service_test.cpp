#include "qingying/app/settings_application_service.hpp"

#include "qingying/app/app_messages.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace qingying {
namespace {

static_assert(noexcept(std::declval<LongShotLimitsProvider&>().update(
    std::declval<const LongShotLimits&>())));

const HWND TestWindow = reinterpret_cast<HWND>(1);

ShortcutBinding alternateCaptureBinding()
{
  return ShortcutBinding{MOD_CONTROL | MOD_ALT, static_cast<UINT>('R')};
}

class FakeSettingsStore;

class FakePreparedSettingsWrite final : public SettingsPreparedWritePort
{
 public:
  FakePreparedSettingsWrite(FakeSettingsStore& store, UserSettings settings)
      : m_store(store),
        m_settings(settings)
  {
  }

  SettingsStoreError commit() noexcept override;
  void cancel() noexcept override;

 private:
  FakeSettingsStore& m_store;
  UserSettings m_settings;
  bool m_active{true};
};

class FakeSettingsStore final : public SettingsStorePort
{
 public:
  UserSettingsLoadResult load() const noexcept override
  {
    return UserSettingsLoadResult{m_settings, m_load_error};
  }

  SettingsStorePrepareResult prepareSave(
      const UserSettings& settings) override
  {
    ++m_prepare_calls;
    if (m_prepare_error != SettingsStoreError::None)
    {
      return SettingsStorePrepareResult{m_prepare_error, nullptr};
    }
    return SettingsStorePrepareResult{
        SettingsStoreError::None,
        std::make_unique<FakePreparedSettingsWrite>(*this, settings)};
  }

  UserSettings m_settings{defaultUserSettings()};
  SettingsStoreError m_load_error{SettingsStoreError::None};
  SettingsStoreError m_prepare_error{SettingsStoreError::None};
  SettingsStoreError m_commit_error{SettingsStoreError::None};
  int m_prepare_calls{0};
  int m_commit_calls{0};
  int m_cancel_calls{0};
};

SettingsStoreError FakePreparedSettingsWrite::commit() noexcept
{
  if (!m_active)
  {
    return SettingsStoreError::PreparedWriteInactive;
  }
  m_active = false;
  ++m_store.m_commit_calls;
  if (m_store.m_commit_error == SettingsStoreError::None)
  {
    m_store.m_settings = m_settings;
  }
  return m_store.m_commit_error;
}

void FakePreparedSettingsWrite::cancel() noexcept
{
  if (!m_active)
  {
    return;
  }
  m_active = false;
  ++m_store.m_cancel_calls;
}

class FakeHotkeyPlatform final
{
 public:
  bool registerHotkey(HWND hwnd, int hotkey_id, UINT modifiers,
                      UINT virtual_key)
  {
    if (hwnd != TestWindow || m_registration_failures > 0 ||
        m_registered_hotkeys.find(hotkey_id) != m_registered_hotkeys.end())
    {
      if (m_registration_failures > 0)
      {
        --m_registration_failures;
      }
      return false;
    }
    m_registered_hotkeys.emplace(hotkey_id,
                                 ShortcutBinding{modifiers, virtual_key});
    return true;
  }

  bool unregisterHotkey(HWND hwnd, int hotkey_id)
  {
    if (hwnd != TestWindow || m_unregister_failures[hotkey_id] > 0)
    {
      if (m_unregister_failures[hotkey_id] > 0)
      {
        --m_unregister_failures[hotkey_id];
      }
      return false;
    }
    return m_registered_hotkeys.erase(hotkey_id) != 0;
  }

  HotkeyPlatformOperations operations()
  {
    return HotkeyPlatformOperations{
        [this](HWND hwnd, int hotkey_id, UINT modifiers, UINT virtual_key)
        {
          return registerHotkey(hwnd, hotkey_id, modifiers, virtual_key);
        },
        [this](HWND hwnd, int hotkey_id)
        {
          return unregisterHotkey(hwnd, hotkey_id);
        }};
  }

  std::map<int, ShortcutBinding> m_registered_hotkeys;
  std::map<int, int> m_unregister_failures;
  int m_registration_failures{0};
};

struct LocalShortcutPreflight
{
  ShortcutBinding m_candidate;
  ShortcutBinding m_binding_to_release;
};

class FakeSystem final
{
 public:
  SettingsSystemPorts ports()
  {
    return SettingsSystemPorts{
        [this]
        {
          return TestWindow;
        },
        [this]
        {
          return m_workflow_active;
        },
        [this]
        {
          return m_autostart_enabled;
        },
        [this](bool enabled)
        {
          m_autostart_writes.push_back(enabled);
          if (m_fail_next_autostart_write ||
              (m_fail_autostart_when_disabled && !enabled))
          {
            m_fail_next_autostart_write = false;
            return false;
          }
          m_autostart_enabled = enabled;
          return true;
        },
        [this]
        {
          return m_agent_enabled;
        },
        [this](bool enabled)
        {
          m_agent_writes.push_back(enabled);
          if (m_fail_next_agent_write)
          {
            m_fail_next_agent_write = false;
            return false;
          }
          m_agent_enabled = enabled;
          return true;
        },
        [this](const ShortcutBinding&)
        {
          return m_capture_hotkey_available;
        },
        [this](const ShortcutBinding& candidate,
               const ShortcutBinding& binding_to_release)
        {
          m_local_preflights.push_back(
              LocalShortcutPreflight{candidate, binding_to_release});
          if (m_required_release_binding.has_value() &&
              binding_to_release != *m_required_release_binding)
          {
            return false;
          }
          return std::find(m_unavailable_local_shortcuts.begin(),
                           m_unavailable_local_shortcuts.end(), candidate) ==
                 m_unavailable_local_shortcuts.end();
        }};
  }

  bool m_workflow_active{false};
  bool m_autostart_enabled{false};
  bool m_agent_enabled{false};
  bool m_capture_hotkey_available{true};
  bool m_fail_next_autostart_write{false};
  bool m_fail_autostart_when_disabled{false};
  bool m_fail_next_agent_write{false};
  std::optional<ShortcutBinding> m_required_release_binding;
  std::vector<ShortcutBinding> m_unavailable_local_shortcuts;
  std::vector<bool> m_autostart_writes;
  std::vector<bool> m_agent_writes;
  std::vector<LocalShortcutPreflight> m_local_preflights;
};

struct ServiceFixture
{
  ServiceFixture()
      : m_hotkeys(m_platform.operations()),
        m_limits(defaultUserSettings().m_longshot_limits)
  {
    EXPECT_TRUE(m_hotkeys.registerCaptureHotkey(
        TestWindow, defaultUserSettings().m_capture_hotkey));
  }

  SettingsApplicationService makeService()
  {
    return SettingsApplicationService(m_store, m_hotkeys, m_limits,
                                      m_system.ports());
  }

  FakeSettingsStore m_store;
  FakeHotkeyPlatform m_platform;
  HotkeyManager m_hotkeys;
  LongShotLimitsProvider m_limits;
  FakeSystem m_system;
};

SettingsDraft currentDraft(SettingsApplicationService& service)
{
  return makeSettingsDraft(service.loadState());
}

bool hasError(const SettingsApplyResult& result,
              SettingsTransactionError error)
{
  return std::find(result.m_errors.begin(), result.m_errors.end(), error) !=
         result.m_errors.end();
}

bool hasRollbackError(const SettingsApplyResult& result,
                      SettingsTransactionError error)
{
  return std::find(result.m_rollback_errors.begin(),
                   result.m_rollback_errors.end(), error) !=
         result.m_rollback_errors.end();
}

TEST(SettingsApplicationServiceTest, ValidationFailurePerformsNoSideEffects)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  SettingsDraft draft = currentDraft(service);
  draft.m_settings.m_longshot_limits.max_frames = 1;

  const SettingsApplyResult result = service.apply(draft);

  EXPECT_FALSE(result.m_committed);
  EXPECT_EQ(result.m_field_errors.m_longshot_limits,
            SettingsFieldError::LongShotLimitsInvalid);
  EXPECT_EQ(fixture.m_store.m_prepare_calls, 0);
  EXPECT_TRUE(fixture.m_system.m_autostart_writes.empty());
  EXPECT_TRUE(fixture.m_system.m_agent_writes.empty());
  EXPECT_EQ(fixture.m_hotkeys.currentCaptureHotkey(),
            defaultUserSettings().m_capture_hotkey);
}

TEST(SettingsApplicationServiceTest,
     ConflictingLocalShortcutLeavesStoredAndGlobalBindingsUntouched)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  SettingsDraft draft = currentDraft(service);
  const ShortcutBinding unavailable{
      MOD_CONTROL | MOD_ALT, static_cast<UINT>('X')};
  draft.m_settings.m_selection_shortcuts.m_copy = unavailable;
  fixture.m_system.m_unavailable_local_shortcuts.push_back(unavailable);

  const SettingsApplyResult result = service.apply(draft);

  EXPECT_FALSE(result.m_committed);
  EXPECT_EQ(result.m_field_errors.m_copy_shortcut,
            SettingsFieldError::CopyShortcutUnavailable);
  EXPECT_EQ(fixture.m_store.m_prepare_calls, 0);
  EXPECT_EQ(fixture.m_hotkeys.currentCaptureHotkey(),
            defaultUserSettings().m_capture_hotkey);
}

TEST(SettingsApplicationServiceTest,
     ConflictingCaptureHotkeyLeavesStoredAndGlobalBindingsUntouched)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  SettingsDraft draft = currentDraft(service);
  draft.m_settings.m_capture_hotkey = alternateCaptureBinding();
  fixture.m_platform.m_registration_failures = 1;

  const SettingsApplyResult result = service.apply(draft);

  EXPECT_FALSE(result.m_committed);
  EXPECT_EQ(result.m_field_errors.m_capture_hotkey,
            SettingsFieldError::CaptureHotkeyUnavailable);
  EXPECT_EQ(fixture.m_store.m_prepare_calls, 0);
  EXPECT_EQ(fixture.m_hotkeys.currentCaptureHotkey(),
            defaultUserSettings().m_capture_hotkey);
}

TEST(SettingsApplicationServiceTest,
     ActiveWorkflowRejectsShortcutChangesButAppliesLongShotLimits)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  fixture.m_system.m_workflow_active = true;
  SettingsDraft shortcut_draft = currentDraft(service);
  shortcut_draft.m_settings.m_capture_hotkey = alternateCaptureBinding();

  const SettingsApplyResult shortcut_result = service.apply(shortcut_draft);

  EXPECT_FALSE(shortcut_result.m_committed);
  EXPECT_EQ(shortcut_result.m_field_errors.m_capture_hotkey,
            SettingsFieldError::ShortcutChangesBlockedByActiveWorkflow);
  EXPECT_EQ(fixture.m_store.m_prepare_calls, 0);

  SettingsDraft limit_draft = currentDraft(service);
  limit_draft.m_settings.m_longshot_limits.max_frames = 35;
  const SettingsApplyResult limit_result = service.apply(limit_draft);

  EXPECT_TRUE(limit_result.m_committed);
  EXPECT_EQ(fixture.m_limits.snapshot().max_frames, 35);
}

TEST(SettingsApplicationServiceTest,
     LocalShortcutCanReuseOldGlobalBindingReleasedByTransaction)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  SettingsDraft draft = currentDraft(service);
  const ShortcutBinding old_capture = draft.m_settings.m_capture_hotkey;
  draft.m_settings.m_capture_hotkey = alternateCaptureBinding();
  draft.m_settings.m_selection_shortcuts.m_copy = old_capture;
  fixture.m_system.m_required_release_binding = old_capture;

  const SettingsApplyResult result = service.apply(draft);

  EXPECT_TRUE(result.m_committed);
  ASSERT_FALSE(fixture.m_system.m_local_preflights.empty());
  EXPECT_EQ(fixture.m_system.m_local_preflights.front().m_binding_to_release,
            old_capture);
  EXPECT_EQ(fixture.m_hotkeys.currentCaptureHotkey(),
            alternateCaptureBinding());
}

TEST(SettingsApplicationServiceTest,
     PrepareFailureCancelsNewHotkeyAndKeepsOldSettingsActive)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  SettingsDraft draft = currentDraft(service);
  draft.m_settings.m_capture_hotkey = alternateCaptureBinding();
  fixture.m_store.m_prepare_error = SettingsStoreError::RegistryWriteFailed;

  const SettingsApplyResult result = service.apply(draft);

  EXPECT_FALSE(result.m_committed);
  EXPECT_TRUE(hasError(result, SettingsTransactionError::SettingsPrepareFailed));
  EXPECT_EQ(fixture.m_hotkeys.currentCaptureHotkey(),
            defaultUserSettings().m_capture_hotkey);
  EXPECT_TRUE(fixture.m_system.m_autostart_writes.empty());
}

TEST(SettingsApplicationServiceTest,
     CommitFailureRestoresSystemSettingsAndKeepsOldHotkey)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  SettingsDraft draft = currentDraft(service);
  draft.m_settings.m_capture_hotkey = alternateCaptureBinding();
  draft.m_autostart_enabled = true;
  fixture.m_store.m_commit_error = SettingsStoreError::RegistryWriteFailed;

  const SettingsApplyResult result = service.apply(draft);

  EXPECT_FALSE(result.m_committed);
  EXPECT_TRUE(hasError(result, SettingsTransactionError::SettingsCommitFailed));
  EXPECT_EQ(fixture.m_system.m_autostart_writes,
            (std::vector<bool>{true, false}));
  EXPECT_FALSE(fixture.m_system.m_autostart_enabled);
  EXPECT_EQ(fixture.m_hotkeys.currentCaptureHotkey(),
            defaultUserSettings().m_capture_hotkey);
}

TEST(SettingsApplicationServiceTest,
     AgentFailureCompensatesAutostartAndAbandonsPreparedSettings)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  SettingsDraft draft = currentDraft(service);
  draft.m_autostart_enabled = true;
  draft.m_agent_enabled = true;
  fixture.m_system.m_fail_next_agent_write = true;

  const SettingsApplyResult result = service.apply(draft);

  EXPECT_FALSE(result.m_committed);
  EXPECT_TRUE(hasError(result, SettingsTransactionError::AgentApplyFailed));
  EXPECT_EQ(fixture.m_system.m_autostart_writes,
            (std::vector<bool>{true, false}));
  EXPECT_EQ(fixture.m_store.m_commit_calls, 0);
  EXPECT_EQ(fixture.m_store.m_cancel_calls, 1);
}

TEST(SettingsApplicationServiceTest,
     FailedCompensationReturnsRefreshedStateAndBothErrors)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  SettingsDraft draft = currentDraft(service);
  draft.m_autostart_enabled = true;
  draft.m_agent_enabled = true;
  fixture.m_system.m_fail_next_agent_write = true;
  fixture.m_system.m_fail_autostart_when_disabled = true;

  const SettingsApplyResult result = service.apply(draft);

  EXPECT_FALSE(result.m_committed);
  EXPECT_TRUE(hasError(result, SettingsTransactionError::AgentApplyFailed));
  EXPECT_TRUE(hasRollbackError(result,
                               SettingsTransactionError::AutostartRollbackFailed));
  EXPECT_TRUE(result.m_state.m_autostart_enabled);
}

TEST(SettingsApplicationServiceTest,
     SuccessfulCommitUpdatesLimitsAfterPersistingTheSnapshot)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  SettingsDraft draft = currentDraft(service);
  draft.m_settings.m_longshot_limits = LongShotLimits{40, 40000};
  draft.m_autostart_enabled = true;
  draft.m_agent_enabled = true;

  const SettingsApplyResult result = service.apply(draft);

  EXPECT_TRUE(result.m_committed);
  EXPECT_EQ(fixture.m_store.m_commit_calls, 1);
  const LongShotLimits limits = fixture.m_limits.snapshot();
  EXPECT_EQ(limits.max_frames, 40);
  EXPECT_EQ(limits.max_output_height, 40000);
  EXPECT_TRUE(result.m_state.m_autostart_enabled);
  EXPECT_TRUE(result.m_state.m_agent_enabled);
}

TEST(SettingsApplicationServiceTest,
     OldHotkeyReleaseWarningDoesNotRejectCommittedSettings)
{
  ServiceFixture fixture;
  SettingsApplicationService service = fixture.makeService();
  SettingsDraft draft = currentDraft(service);
  const ShortcutBinding old_capture = draft.m_settings.m_capture_hotkey;
  draft.m_settings.m_capture_hotkey = alternateCaptureBinding();
  draft.m_settings.m_selection_shortcuts.m_copy = old_capture;
  fixture.m_system.m_required_release_binding = old_capture;
  fixture.m_platform.m_unregister_failures[HotkeyIds::kCapturePrimary] = 1;

  const SettingsApplyResult result = service.apply(draft);

  EXPECT_TRUE(result.m_committed);
  EXPECT_TRUE(result.m_old_hotkey_release_pending);
  EXPECT_EQ(fixture.m_hotkeys.currentCaptureHotkey(),
            alternateCaptureBinding());
  EXPECT_FALSE(
      fixture.m_hotkeys.isCurrentCaptureHotkeyId(HotkeyIds::kCapturePrimary));
}

TEST(SettingsApplicationServiceTest,
     LoadStatePreservesSavedHotkeyWhenAvailabilityPrecheckFails)
{
  ServiceFixture fixture;
  fixture.m_system.m_capture_hotkey_available = false;
  SettingsApplicationService service = fixture.makeService();

  const SettingsState state = service.loadState();

  EXPECT_EQ(state.m_settings.m_capture_hotkey,
            defaultUserSettings().m_capture_hotkey);
  EXPECT_FALSE(state.m_capture_hotkey_available);
  EXPECT_EQ(fixture.m_store.m_prepare_calls, 0);
}

}  // namespace
}  // namespace qingying
