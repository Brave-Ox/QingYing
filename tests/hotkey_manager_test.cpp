#include "qingying/app/hotkey_manager.hpp"

#include "qingying/app/app_messages.hpp"

#include <gtest/gtest.h>

#include <map>
#include <type_traits>
#include <utility>

namespace qingying {
namespace {

const HWND TestWindow = reinterpret_cast<HWND>(1);

struct RegisteredHotkey
{
  UINT m_modifiers{0};
  UINT m_virtual_key{0};
};

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
    m_registered_hotkeys.emplace(
        hotkey_id, RegisteredHotkey{modifiers, virtual_key});
    return true;
  }

  bool unregisterHotkey(HWND hwnd, int hotkey_id)
  {
    ++m_unregister_attempts[hotkey_id];
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
        [this](HWND hwnd, int hotkey_id, UINT modifiers, UINT virtual_key) {
          return registerHotkey(hwnd, hotkey_id, modifiers, virtual_key);
        },
        [this](HWND hwnd, int hotkey_id) {
          return unregisterHotkey(hwnd, hotkey_id);
        }};
  }

  bool hasRegisteredHotkey(int hotkey_id) const
  {
    return m_registered_hotkeys.find(hotkey_id) != m_registered_hotkeys.end();
  }

  int m_registration_failures{0};
  std::map<int, int> m_unregister_failures;
  std::map<int, int> m_unregister_attempts;
  std::map<int, RegisteredHotkey> m_registered_hotkeys;
};

ShortcutBinding defaultCaptureBinding()
{
  return ShortcutBinding{MOD_CONTROL | MOD_SHIFT, static_cast<UINT>('Q')};
}

ShortcutBinding alternateCaptureBinding()
{
  return ShortcutBinding{MOD_CONTROL | MOD_ALT, static_cast<UINT>('R')};
}

TEST(HotkeyManagerTest, RegistersDefaultBindingRejectsDuplicateAndUnregisters)
{
  FakeHotkeyPlatform platform;
  HotkeyManager manager(platform.operations());

  ASSERT_TRUE(manager.registerCaptureHotkey(TestWindow, defaultCaptureBinding()));
  EXPECT_EQ(manager.currentCaptureHotkey(), defaultCaptureBinding());
  EXPECT_TRUE(manager.isCurrentCaptureHotkeyId(HotkeyIds::kCapturePrimary));
  EXPECT_FALSE(manager.registerCaptureHotkey(TestWindow, defaultCaptureBinding()));

  manager.unregisterAll(TestWindow);
  EXPECT_TRUE(platform.m_registered_hotkeys.empty());
  EXPECT_FALSE(manager.isCurrentCaptureHotkeyId(HotkeyIds::kCapturePrimary));
}

TEST(HotkeyManagerTest, PrepareFailureKeepsThePreviousBindingRegistered)
{
  FakeHotkeyPlatform platform;
  HotkeyManager manager(platform.operations());
  ASSERT_TRUE(manager.registerCaptureHotkey(TestWindow, defaultCaptureBinding()));

  platform.m_registration_failures = 1;
  PreparedCaptureHotkey prepared =
      manager.prepareCaptureHotkey(TestWindow, alternateCaptureBinding());

  EXPECT_FALSE(prepared.valid());
  EXPECT_EQ(manager.currentCaptureHotkey(), defaultCaptureBinding());
  EXPECT_TRUE(platform.hasRegisteredHotkey(HotkeyIds::kCapturePrimary));
  EXPECT_FALSE(platform.hasRegisteredHotkey(HotkeyIds::kCaptureSecondary));
}

TEST(HotkeyManagerTest, UncommittedPreparedTokenReleasesOnlyTheNewBinding)
{
  FakeHotkeyPlatform platform;
  HotkeyManager manager(platform.operations());
  ASSERT_TRUE(manager.registerCaptureHotkey(TestWindow, defaultCaptureBinding()));

  {
    PreparedCaptureHotkey prepared =
        manager.prepareCaptureHotkey(TestWindow, alternateCaptureBinding());
    ASSERT_TRUE(prepared.valid());
    EXPECT_TRUE(platform.hasRegisteredHotkey(HotkeyIds::kCapturePrimary));
    EXPECT_TRUE(platform.hasRegisteredHotkey(HotkeyIds::kCaptureSecondary));
  }

  EXPECT_TRUE(platform.hasRegisteredHotkey(HotkeyIds::kCapturePrimary));
  EXPECT_FALSE(platform.hasRegisteredHotkey(HotkeyIds::kCaptureSecondary));
  EXPECT_EQ(manager.currentCaptureHotkey(), defaultCaptureBinding());
}

TEST(HotkeyManagerTest, CommitMakesNewBindingCurrentAndCannotBeRepeated)
{
  FakeHotkeyPlatform platform;
  HotkeyManager manager(platform.operations());
  ASSERT_TRUE(manager.registerCaptureHotkey(TestWindow, defaultCaptureBinding()));
  PreparedCaptureHotkey prepared =
      manager.prepareCaptureHotkey(TestWindow, alternateCaptureBinding());
  ASSERT_TRUE(prepared.valid());

  EXPECT_EQ(prepared.commit(), HotkeyCommitResult::Committed);
  EXPECT_EQ(manager.currentCaptureHotkey(), alternateCaptureBinding());
  EXPECT_TRUE(manager.isCurrentCaptureHotkeyId(HotkeyIds::kCaptureSecondary));
  EXPECT_FALSE(manager.isCurrentCaptureHotkeyId(HotkeyIds::kCapturePrimary));
  EXPECT_FALSE(platform.hasRegisteredHotkey(HotkeyIds::kCapturePrimary));
  EXPECT_TRUE(platform.hasRegisteredHotkey(HotkeyIds::kCaptureSecondary));
  EXPECT_EQ(prepared.commit(), HotkeyCommitResult::PreparedTokenInactive);
}

TEST(HotkeyManagerTest, MaintenanceRetriesAStaleOldBindingWithoutRoutingIt)
{
  FakeHotkeyPlatform platform;
  HotkeyManager manager(platform.operations());
  ASSERT_TRUE(manager.registerCaptureHotkey(TestWindow, defaultCaptureBinding()));
  platform.m_unregister_failures[HotkeyIds::kCapturePrimary] = 1;
  PreparedCaptureHotkey prepared =
      manager.prepareCaptureHotkey(TestWindow, alternateCaptureBinding());
  ASSERT_TRUE(prepared.valid());

  EXPECT_EQ(prepared.commit(),
            HotkeyCommitResult::CommittedWithOldBindingPendingRelease);
  EXPECT_TRUE(platform.hasRegisteredHotkey(HotkeyIds::kCapturePrimary));
  EXPECT_TRUE(manager.isCurrentCaptureHotkeyId(HotkeyIds::kCaptureSecondary));
  EXPECT_FALSE(manager.isCurrentCaptureHotkeyId(HotkeyIds::kCapturePrimary));

  manager.maintenance(TestWindow);
  EXPECT_FALSE(platform.hasRegisteredHotkey(HotkeyIds::kCapturePrimary));
  EXPECT_GE(platform.m_unregister_attempts[HotkeyIds::kCapturePrimary], 2);
}

TEST(HotkeyManagerTest, PreparingAgainRetriesOldBindingCleanupBeforeUsingItsId)
{
  FakeHotkeyPlatform platform;
  HotkeyManager manager(platform.operations());
  ASSERT_TRUE(manager.registerCaptureHotkey(TestWindow, defaultCaptureBinding()));
  platform.m_unregister_failures[HotkeyIds::kCapturePrimary] = 1;
  PreparedCaptureHotkey first =
      manager.prepareCaptureHotkey(TestWindow, alternateCaptureBinding());
  ASSERT_EQ(first.commit(),
            HotkeyCommitResult::CommittedWithOldBindingPendingRelease);

  PreparedCaptureHotkey second =
      manager.prepareCaptureHotkey(TestWindow, defaultCaptureBinding());
  EXPECT_TRUE(second.valid());
  EXPECT_TRUE(platform.hasRegisteredHotkey(HotkeyIds::kCapturePrimary));
  second.cancel();
}

TEST(HotkeyManagerTest, UnregisterAllRetriesPendingCleanupBeforeCurrentBinding)
{
  FakeHotkeyPlatform platform;
  HotkeyManager manager(platform.operations());
  ASSERT_TRUE(manager.registerCaptureHotkey(TestWindow, defaultCaptureBinding()));
  platform.m_unregister_failures[HotkeyIds::kCapturePrimary] = 1;
  PreparedCaptureHotkey prepared =
      manager.prepareCaptureHotkey(TestWindow, alternateCaptureBinding());
  ASSERT_EQ(prepared.commit(),
            HotkeyCommitResult::CommittedWithOldBindingPendingRelease);

  manager.unregisterAll(TestWindow);
  EXPECT_TRUE(platform.m_registered_hotkeys.empty());
  EXPECT_FALSE(manager.isCurrentCaptureHotkeyId(HotkeyIds::kCaptureSecondary));
}

TEST(HotkeyManagerTest, PreparedTokenIsMoveOnly)
{
  static_assert(!std::is_copy_constructible_v<PreparedCaptureHotkey>);
  static_assert(!std::is_copy_assignable_v<PreparedCaptureHotkey>);
  static_assert(std::is_move_constructible_v<PreparedCaptureHotkey>);

  FakeHotkeyPlatform platform;
  HotkeyManager manager(platform.operations());
  ASSERT_TRUE(manager.registerCaptureHotkey(TestWindow, defaultCaptureBinding()));
  PreparedCaptureHotkey prepared =
      manager.prepareCaptureHotkey(TestWindow, alternateCaptureBinding());
  PreparedCaptureHotkey moved = std::move(prepared);

  EXPECT_FALSE(prepared.valid());
  EXPECT_TRUE(moved.valid());
}

}  // namespace
}  // namespace qingying
