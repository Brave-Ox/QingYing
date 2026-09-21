#include "qingying/app/tray_context_menu.hpp"

#include <array>

#include <gtest/gtest.h>

#include "qingying/app/tray_controller.hpp"

namespace qingying {

TEST(TrayContextMenuTest, AutostartAndExitLabelsAreChinese)
{
  EXPECT_STREQ(TrayMenuAutostartText, L"开机自启");
  EXPECT_STREQ(TrayMenuExitText, L"退出");
  EXPECT_STREQ(trayMenuLabelForId(TrayMenuAutostartCommandId), L"开机自启");
  EXPECT_STREQ(trayMenuLabelForId(TrayMenuExitCommandId), L"退出");
}

TEST(TrayContextMenuTest, CaptureAndSettingsAppearBeforeSystemSwitches)
{
  EXPECT_STREQ(TrayMenuCaptureText, L"开始截图");
  EXPECT_STREQ(TrayMenuSettingsText, L"设置...");
  EXPECT_STREQ(trayMenuLabelForId(TrayMenuCaptureCommandId), L"开始截图");
  EXPECT_STREQ(trayMenuLabelForId(TrayMenuSettingsCommandId), L"设置...");

  const std::array<UINT, 5> expected{
      TrayMenuCaptureCommandId,
      TrayMenuSettingsCommandId,
      TrayMenuAutostartCommandId,
      TrayMenuAutomationCommandId,
      TrayMenuExitCommandId};
  EXPECT_EQ(trayMenuCommandOrder(), expected);
  EXPECT_EQ(trayMenuCaptureDisplayText(L"Ctrl + Shift + Q"),
            L"开始截图\tCtrl + Shift + Q");
}

TEST(TrayControllerTest, DispatchesCaptureAndSettingsCommandsToCallbacks)
{
  TrayController tray;
  ASSERT_TRUE(tray.create(GetModuleHandleW(nullptr)));
  int capture_requests = 0;
  int settings_requests = 0;
  tray.setBeginCaptureCallback([&capture_requests]() { ++capture_requests; });
  tray.setSettingsCallback([&settings_requests]() { ++settings_requests; });

  SendMessageW(tray.hwnd(), WM_COMMAND, TrayMenuCaptureCommandId, 0);
  SendMessageW(tray.hwnd(), WM_COMMAND, TrayMenuSettingsCommandId, 0);

  EXPECT_EQ(capture_requests, 1);
  EXPECT_EQ(settings_requests, 1);
  tray.destroy();
}

TEST(TrayControllerTest, AutomationCommandUsesTheCurrentStateAfterCallbackSync)
{
  TrayController tray;
  ASSERT_TRUE(tray.create(GetModuleHandleW(nullptr)));

  std::vector<bool> requested_states;
  tray.setAutomationToggle([&tray, &requested_states](bool enabled) {
    requested_states.push_back(enabled);
    tray.setAutomationEnabled(enabled);
    return true;
  });

  SendMessageW(tray.hwnd(), WM_COMMAND, TrayMenuAutomationCommandId, 0);
  SendMessageW(tray.hwnd(), WM_COMMAND, TrayMenuAutomationCommandId, 0);

  const std::vector<bool> expected{true, false};
  EXPECT_EQ(requested_states, expected);
  tray.destroy();
}

TEST(TrayContextMenuTest, FontIsMicrosoftYaHeiUiAt14Px)
{
  EXPECT_STREQ(TrayMenuFontFace, L"Microsoft YaHei UI");
  EXPECT_EQ(TrayMenuFontSizePx, 14);

  const LOGFONTW font = trayMenuLogFont(TrayMenuDefaultDpi);
  EXPECT_EQ(font.lfHeight, -TrayMenuFontSizePx);
  EXPECT_EQ(font.lfWeight, FW_NORMAL);
  EXPECT_EQ(font.lfQuality, CLEARTYPE_QUALITY);
  EXPECT_STREQ(font.lfFaceName, L"Microsoft YaHei UI");
}

TEST(TrayContextMenuTest, MenuItemsAreOwnerDrawnWithComfortablePadding)
{
  MENUITEMINFOW autostart = {};
  fillTrayMenuItem(autostart, TrayMenuAutostartCommandId, true);
  EXPECT_NE(autostart.fType & MFT_OWNERDRAW, 0u);
  EXPECT_NE(autostart.fState & MFS_CHECKED, 0u);

  MENUITEMINFOW exit_item = {};
  fillTrayMenuItem(exit_item, TrayMenuExitCommandId, false);
  EXPECT_NE(exit_item.fType & MFT_OWNERDRAW, 0u);
  EXPECT_EQ(exit_item.fState & MFS_CHECKED, 0u);

  const int item_height = trayMenuMeasureHeight(
      static_cast<ULONG_PTR>(TrayMenuItemKind::Autostart), TrayMenuDefaultDpi);
  const int separator_height = trayMenuMeasureHeight(
      static_cast<ULONG_PTR>(TrayMenuItemKind::Separator), TrayMenuDefaultDpi);
  EXPECT_EQ(item_height, TrayMenuFontSizePx + TrayMenuPadY * 2);
  EXPECT_LT(separator_height, item_height);
  EXPECT_GE(item_height, 30);
}

TEST(TrayContextMenuTest, HoverCardUsesALightBlueFlatHighlight)
{
  EXPECT_EQ(TrayMenuHoverCardFill, RGB(238, 245, 255));
  EXPECT_EQ(TrayMenuHoverCardBorder, RGB(229, 236, 245));
  EXPECT_EQ(TrayMenuHoverRadius, 6);
  EXPECT_EQ(TrayMenuSeparatorColor, RGB(236, 239, 244));

  RECT item = {};
  item.right = 160;
  item.bottom = trayMenuItemHeightPx(TrayMenuDefaultDpi);
  const RECT card = trayMenuHoverCardRect(item, TrayMenuDefaultDpi);
  EXPECT_EQ(card.left, TrayMenuHoverInsetX);
  EXPECT_EQ(card.right, item.right - TrayMenuHoverInsetX);
  EXPECT_EQ(card.top, TrayMenuHoverInsetY);
  EXPECT_EQ(card.bottom, item.bottom - TrayMenuHoverInsetY);
}

}  // namespace qingying
