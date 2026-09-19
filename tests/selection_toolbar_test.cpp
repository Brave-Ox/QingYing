#include "qingying/overlay/selection_toolbar.hpp"
#include "qingying/ui/shortcut_types.hpp"

#include <gtest/gtest.h>

#include "qingying/ui/toolbar_model.h"

namespace qingying {

namespace {

HWND findCurrentThreadToolbarWindow()
{
  HWND result = nullptr;
  EnumThreadWindows(
      GetCurrentThreadId(),
      [](HWND hwnd, LPARAM lparam) -> BOOL
      {
        wchar_t class_name[128]{};
        if (GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name)) > 0 &&
            lstrcmpW(class_name, L"QingYingSelectionToolbarV4") == 0)
        {
          *reinterpret_cast<HWND*>(lparam) = hwnd;
          return FALSE;
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&result));
  return result;
}

const SelectionToolbarItemModel& itemFor(const SelectionToolbarItems& items,
                                         SelectionToolbarCommand command) {
  for (const SelectionToolbarItemModel& item : items) {
    if (item.command == command) {
      return item;
    }
  }
  return items.front();
}

SelectionShortcutSettings defaultSelectionShortcuts()
{
  return SelectionShortcutSettings{
      ShortcutBinding{MOD_CONTROL, static_cast<UINT>('C')},
      ShortcutBinding{0, static_cast<UINT>('L')}};
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

TEST(SelectionToolbarTest, RecoverableWithoutImageDisablesResultAcceptance) {
  const LongShotRecoveryState recovery{
      LongShotRecoveryResult::None, LongShotRecoveryCause::InputUnavailable};
  const SelectionToolbarItems items = buildSelectionToolbarItems(
      OverlayPhase::LongShotRecoverable, recovery);

  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::RetryLongShot).enabled);
  EXPECT_TRUE(
      itemFor(items, SelectionToolbarCommand::AdjustLongShotSelection).enabled);
  EXPECT_FALSE(
      itemFor(items, SelectionToolbarCommand::KeepLongShotFrame).enabled);
  EXPECT_TRUE(
      itemFor(items, SelectionToolbarCommand::CancelLongShot).enabled);
  EXPECT_EQ(selectionToolbarStatus(OverlayPhase::LongShotRecoverable,
                                   recovery),
            SelectionToolbarStatus::Recoverable);
}

TEST(SelectionToolbarTest, SingleFrameIsPresentedAsOrdinaryCapture) {
  const LongShotRecoveryState recovery{
      LongShotRecoveryResult::SingleFrame, LongShotRecoveryCause::NoProgress};
  const SelectionToolbarItems items = buildSelectionToolbarItems(
      OverlayPhase::LongShotResultPending, recovery);

  EXPECT_TRUE(
      itemFor(items, SelectionToolbarCommand::KeepLongShotFrame).enabled);
  EXPECT_EQ(items[2].command, SelectionToolbarCommand::KeepLongShotFrame);
  EXPECT_EQ(selectionToolbarStatus(OverlayPhase::LongShotResultPending,
                                   recovery),
            SelectionToolbarStatus::SingleFramePending);
}

TEST(SelectionToolbarTest, VerifiedCompositeEnablesPartialResultAcceptance) {
  const LongShotRecoveryState recovery{
      LongShotRecoveryResult::PartialResult,
      LongShotRecoveryCause::MatchFailed};
  const SelectionToolbarItems items = buildSelectionToolbarItems(
      OverlayPhase::LongShotResultPending, recovery);

  EXPECT_TRUE(itemFor(items,
                      SelectionToolbarCommand::AcceptLongShotPartial)
                  .enabled);
  EXPECT_EQ(items[2].command,
            SelectionToolbarCommand::AcceptLongShotPartial);
  EXPECT_EQ(selectionToolbarStatus(OverlayPhase::LongShotResultPending,
                                   recovery),
            SelectionToolbarStatus::PartialResultPending);
}

TEST(SelectionToolbarTest, LongShotLifecycleHasDistinctStatusTextStates) {
  EXPECT_EQ(selectionToolbarStatus(OverlayPhase::LongShotRunning),
            SelectionToolbarStatus::Running);
  EXPECT_EQ(selectionToolbarStatus(OverlayPhase::LongShotPaused),
            SelectionToolbarStatus::Paused);
  EXPECT_EQ(selectionToolbarStatus(OverlayPhase::LongShotFinishing),
            SelectionToolbarStatus::Finishing);
}

TEST(SelectionToolbarTest, CopyFailureKeepsPublishedResultActionsEnabled) {
  const SelectionToolbarItems items = buildSelectionToolbarItems(
      OverlayPhase::Selected, {}, LongShotResultNotice::CopyFailed);

  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Copy).enabled);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Save).enabled);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Edit).enabled);
  EXPECT_TRUE(itemFor(items, SelectionToolbarCommand::Pin).enabled);
  EXPECT_EQ(selectionToolbarStatus(OverlayPhase::Selected, {},
                                   LongShotResultNotice::CopyFailed),
            SelectionToolbarStatus::CopyFailed);
}

TEST(SelectionToolbarTest, ShortcutsMatchEnabledToolbarCommands)
{
  SelectionToolbarCommand command = SelectionToolbarCommand::Cancel;
  const SelectionShortcutSettings shortcuts = defaultSelectionShortcuts();

  EXPECT_TRUE(selectionToolbarShortcutCommand(
      OverlayPhase::Selected, shortcuts,
      ShortcutBinding{MOD_CONTROL, static_cast<UINT>('C')}, command));
  EXPECT_EQ(command, SelectionToolbarCommand::Copy);

  EXPECT_TRUE(selectionToolbarShortcutCommand(
      OverlayPhase::Selected, shortcuts,
      ShortcutBinding{0, static_cast<UINT>('L')}, command));
  EXPECT_EQ(command, SelectionToolbarCommand::ToggleLongShot);

  EXPECT_FALSE(selectionToolbarShortcutCommand(
      OverlayPhase::Selected, shortcuts,
      ShortcutBinding{0, static_cast<UINT>('C')}, command));
  EXPECT_FALSE(selectionToolbarShortcutCommand(
      OverlayPhase::Selected, shortcuts,
      ShortcutBinding{MOD_CONTROL, static_cast<UINT>('L')}, command));
  EXPECT_FALSE(selectionToolbarShortcutCommand(
      OverlayPhase::LongShotRunning, shortcuts,
      ShortcutBinding{MOD_CONTROL, static_cast<UINT>('C')}, command));

  EXPECT_TRUE(selectionToolbarShortcutCommand(
      OverlayPhase::LongShotRunning, shortcuts,
      ShortcutBinding{0, static_cast<UINT>('L')}, command));
  EXPECT_EQ(command, SelectionToolbarCommand::ToggleLongShot);
}

TEST(SelectionToolbarTest, HotkeysRouteWithoutKeyboardFocus)
{
  SelectionToolbarCommand command = SelectionToolbarCommand::Cancel;
  const SelectionShortcutSettings shortcuts = defaultSelectionShortcuts();

  EXPECT_TRUE(selectionToolbarHotkeyCommand(
      OverlayPhase::Selected, shortcuts, SelectionToolbarCopyHotkeyId,
      command));
  EXPECT_EQ(command, SelectionToolbarCommand::Copy);

  EXPECT_TRUE(selectionToolbarHotkeyCommand(
      OverlayPhase::Selected, shortcuts, SelectionToolbarLongShotHotkeyId,
      command));
  EXPECT_EQ(command, SelectionToolbarCommand::ToggleLongShot);

  EXPECT_FALSE(selectionToolbarHotkeyCommand(OverlayPhase::Selected, shortcuts,
                                             -1, command));
}

TEST(SelectionToolbarTest, CustomShortcutSnapshotRoutesConfiguredCommands)
{
  const SelectionShortcutSettings shortcuts{
      ShortcutBinding{MOD_ALT, static_cast<UINT>('X')},
      ShortcutBinding{MOD_SHIFT, static_cast<UINT>('Y')}};
  SelectionToolbarCommand command = SelectionToolbarCommand::Cancel;

  EXPECT_TRUE(selectionToolbarShortcutCommand(
      OverlayPhase::Selected, shortcuts,
      ShortcutBinding{MOD_ALT, static_cast<UINT>('X')}, command));
  EXPECT_EQ(command, SelectionToolbarCommand::Copy);

  EXPECT_TRUE(selectionToolbarHotkeyCommand(
      OverlayPhase::Selected, shortcuts, SelectionToolbarLongShotHotkeyId,
      command));
  EXPECT_EQ(command, SelectionToolbarCommand::ToggleLongShot);
}

TEST(SelectionToolbarTest, EmptyConfiguredShortcutsDoNotRouteCommands)
{
  const SelectionShortcutSettings shortcuts{};
  SelectionToolbarCommand command = SelectionToolbarCommand::Cancel;

  EXPECT_FALSE(selectionToolbarHotkeyCommand(
      OverlayPhase::Selected, shortcuts, SelectionToolbarCopyHotkeyId,
      command));
  EXPECT_FALSE(selectionToolbarHotkeyCommand(
      OverlayPhase::Selected, shortcuts, SelectionToolbarLongShotHotkeyId,
      command));
}

TEST(SelectionToolbarTest, InitialAndHoverFramesUseAtomicLayeredPresentation)
{
  SelectionToolbar toolbar;
  const SelectionToolbarPlacement placement{
      100, 100, 320, 180, 0, 0, 1920, 1080};
  ASSERT_TRUE(toolbar.show(nullptr, placement, OverlayPhase::Selected,
                           [](SelectionToolbarCommand) {}));

  const HWND hwnd = findCurrentThreadToolbarWindow();
  ASSERT_NE(hwnd, nullptr);
  ASSERT_NE(GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED, 0);

  // show() 返回时首帧必须已经提交，不能把透明窗口留到消息队列空闲时
  // 才处理 WM_PAINT；框选结束后的整屏 Overlay 重绘会放大这段延迟。
  RECT pending_paint{};
  EXPECT_FALSE(GetUpdateRect(hwnd, &pending_paint, FALSE));

  RECT window_rect{};
  ASSERT_TRUE(GetWindowRect(hwnd, &window_rect));
  const POINT toolbar_center{
      (window_rect.left + window_rect.right) / 2,
      (window_rect.top + window_rect.bottom) / 2};
  EXPECT_EQ(WindowFromPoint(toolbar_center), hwnd);

  // SetLayeredWindowAttributes + LWA_COLORKEY 会让 hover 重绘期间的透明
  // 清屏成为可见中间帧。工具栏必须只使用 UpdateLayeredWindow 的整帧提交。
  COLORREF color_key = 0;
  BYTE alpha = 0;
  DWORD flags = 0;
  const BOOL has_global_layer_attributes =
      GetLayeredWindowAttributes(hwnd, &color_key, &alpha, &flags);
  EXPECT_TRUE(has_global_layer_attributes == FALSE ||
              (flags & LWA_COLORKEY) == 0);

  SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(14, 22));
  UpdateWindow(hwnd);
  EXPECT_TRUE(IsWindowVisible(hwnd));
  EXPECT_EQ(WindowFromPoint(toolbar_center), hwnd);

  color_key = 0;
  alpha = 0;
  flags = 0;
  const BOOL hover_has_global_layer_attributes =
      GetLayeredWindowAttributes(hwnd, &color_key, &alpha, &flags);
  EXPECT_TRUE(hover_has_global_layer_attributes == FALSE ||
              (flags & LWA_COLORKEY) == 0);

  toolbar.hide();
  EXPECT_FALSE(toolbar.visible());
}

TEST(SelectionToolbarTest, StatusToolbarUsesSideSpaceBeforeCoveringSelection)
{
  SelectionToolbar toolbar;
  const SelectionToolbarPlacement placement{
      400, 10, 400, 980, 0, 0, 1200, 1000};
  ASSERT_TRUE(toolbar.show(nullptr, placement,
                           OverlayPhase::LongShotRecoverable,
                           [](SelectionToolbarCommand) {},
                           {LongShotRecoveryResult::None,
                            LongShotRecoveryCause::InputUnavailable}));

  const HWND hwnd = findCurrentThreadToolbarWindow();
  ASSERT_NE(hwnd, nullptr);
  RECT rect{};
  ASSERT_TRUE(GetWindowRect(hwnd, &rect));
  EXPECT_GT(rect.bottom - rect.top,
            modernToolbarHeight(DefaultModernToolbarMetrics));
  EXPECT_TRUE(rect.left >= placement.selection_x + placement.selection_width ||
              rect.right <= placement.selection_x);

  toolbar.hide();
}

}  // namespace qingying
