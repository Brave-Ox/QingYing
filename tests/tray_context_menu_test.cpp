#include "qingying/app/tray_context_menu.hpp"

#include <gtest/gtest.h>

namespace qingying {

TEST(TrayContextMenuTest, AutostartAndExitLabelsAreChinese)
{
  EXPECT_STREQ(TrayMenuAutostartText, L"开机自启");
  EXPECT_STREQ(TrayMenuExitText, L"退出");
  EXPECT_STREQ(trayMenuLabelForId(TrayMenuAutostartCommandId), L"开机自启");
  EXPECT_STREQ(trayMenuLabelForId(TrayMenuExitCommandId), L"退出");
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

TEST(TrayContextMenuTest, HoverCardLeavesRoomForQqMusicStyleDropShadow)
{
  EXPECT_GE(TrayMenuHoverShadowOffsetX, 2);
  EXPECT_GE(TrayMenuHoverShadowOffsetY, 3);
  EXPECT_GE(TrayMenuHoverShadowLayers, 4);
  EXPECT_GE(TrayMenuHoverRadius, 6);

  const int shadow_sum = trayMenuRed(TrayMenuHoverShadow) +
                         trayMenuGreen(TrayMenuHoverShadow) +
                         trayMenuBlue(TrayMenuHoverShadow);
  const int fill_sum = trayMenuRed(TrayMenuFill) + trayMenuGreen(TrayMenuFill) +
                       trayMenuBlue(TrayMenuFill);
  EXPECT_LE(shadow_sum, 520);
  EXPECT_GE(fill_sum - shadow_sum, 240);

  RECT item = {};
  item.right = 160;
  item.bottom = trayMenuItemHeightPx(TrayMenuDefaultDpi);
  const RECT card = trayMenuHoverCardRect(item, TrayMenuDefaultDpi);
  EXPECT_GT(card.left, item.left);
  EXPECT_LT(card.right, item.right);
  EXPECT_GT(card.top, item.top);
  EXPECT_LT(card.bottom, item.bottom);
  EXPECT_LE(card.bottom + TrayMenuHoverShadowOffsetY, item.bottom);
}

}  // namespace qingying
