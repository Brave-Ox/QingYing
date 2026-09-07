#include "qingying/overlay/selection_toolbar.hpp"

#include <gtest/gtest.h>

namespace qingying {

namespace {

const SelectionToolbarItemModel& itemFor(const SelectionToolbarItems& items,
                                         SelectionToolbarCommand command) {
  for (const SelectionToolbarItemModel& item : items) {
    if (item.command == command) {
      return item;
    }
  }
  return items.front();
}

}  // namespace

TEST(SelectionToolbarTest, SelectedPhaseEnablesResultActionsAndLongShotStart) {
  const SelectionToolbarItems items =
      buildSelectionToolbarItems(OverlayPhase::Selected);

  EXPECT_EQ(items.size(), SelectionToolbarItemCount);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Copy).enabled);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Save).enabled);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Edit).enabled);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Pin).enabled);
  const auto& toggle =
      itemFor(items, SelectionToolbarCommand::ToggleLongShot);
  EXPECT_TRUE(toggle.enabled);
  EXPECT_EQ(toggle.icon, SelectionToolbarIcon::LongShot);
  EXPECT_FALSE(itemFor(items, SelectionToolbarCommand::StopLongShot).enabled);
}

TEST(SelectionToolbarTest, RunningPhaseOnlyEnablesPauseAndStop) {
  const SelectionToolbarItems items =
      buildSelectionToolbarItems(OverlayPhase::LongShotRunning);

  EXPECT_FALSE(itemFor(items, SelectionToolbarCommand::Copy).enabled);
  EXPECT_FALSE(itemFor(items, SelectionToolbarCommand::Save).enabled);
  EXPECT_FALSE(itemFor(items, SelectionToolbarCommand::Edit).enabled);
  EXPECT_FALSE(itemFor(items, SelectionToolbarCommand::Pin).enabled);
  const auto& toggle =
      itemFor(items, SelectionToolbarCommand::ToggleLongShot);
  EXPECT_TRUE(toggle.enabled);
  EXPECT_EQ(toggle.icon, SelectionToolbarIcon::Pause);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::StopLongShot).enabled);
}

TEST(SelectionToolbarTest, PausedPhaseAllowsResultActionAfterStopping) {
  const SelectionToolbarItems items =
      buildSelectionToolbarItems(OverlayPhase::LongShotPaused);

  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Copy).enabled);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Save).enabled);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Edit).enabled);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Pin).enabled);
  const auto& toggle =
      itemFor(items, SelectionToolbarCommand::ToggleLongShot);
  EXPECT_TRUE(toggle.enabled);
  EXPECT_EQ(toggle.icon, SelectionToolbarIcon::Resume);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::StopLongShot).enabled);
}

TEST(SelectionToolbarTest, FinishingPhaseDisablesEveryCommand) {
  const SelectionToolbarItems items =
      buildSelectionToolbarItems(OverlayPhase::LongShotFinishing);

  for (const SelectionToolbarItemModel& item : items) {
    EXPECT_FALSE(item.enabled);
  }
}

TEST(SelectionToolbarTest, ShortcutsMatchEnabledToolbarCommands)
{
  SelectionToolbarCommand command = SelectionToolbarCommand::Cancel;

  EXPECT_TRUE(selectionToolbarShortcutCommand(OverlayPhase::Selected, true,
                                              SelectionToolbarCopyShortcutVirtualKey,
                                              command));
  EXPECT_EQ(command, SelectionToolbarCommand::Copy);

  EXPECT_TRUE(selectionToolbarShortcutCommand(OverlayPhase::Selected, false,
                                              SelectionToolbarLongShotShortcutVirtualKey,
                                              command));
  EXPECT_EQ(command, SelectionToolbarCommand::ToggleLongShot);

  EXPECT_FALSE(selectionToolbarShortcutCommand(OverlayPhase::Selected, false,
                                               SelectionToolbarCopyShortcutVirtualKey,
                                               command));
  EXPECT_FALSE(selectionToolbarShortcutCommand(OverlayPhase::Selected, true,
                                               SelectionToolbarLongShotShortcutVirtualKey,
                                               command));
  EXPECT_FALSE(selectionToolbarShortcutCommand(OverlayPhase::LongShotRunning,
                                               true,
                                               SelectionToolbarCopyShortcutVirtualKey,
                                               command));

  EXPECT_TRUE(selectionToolbarShortcutCommand(OverlayPhase::LongShotRunning,
                                              false,
                                              SelectionToolbarLongShotShortcutVirtualKey,
                                              command));
  EXPECT_EQ(command, SelectionToolbarCommand::ToggleLongShot);
}

TEST(SelectionToolbarTest, HotkeysRouteWithoutKeyboardFocus)
{
  SelectionToolbarCommand command = SelectionToolbarCommand::Cancel;

  EXPECT_TRUE(selectionToolbarHotkeyCommand(
      OverlayPhase::Selected, SelectionToolbarCopyHotkeyId, command));
  EXPECT_EQ(command, SelectionToolbarCommand::Copy);

  EXPECT_TRUE(selectionToolbarHotkeyCommand(
      OverlayPhase::Selected, SelectionToolbarLongShotHotkeyId, command));
  EXPECT_EQ(command, SelectionToolbarCommand::ToggleLongShot);

  EXPECT_FALSE(selectionToolbarHotkeyCommand(OverlayPhase::Selected, -1,
                                             command));
}

}  // namespace qingying
