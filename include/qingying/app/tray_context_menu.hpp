#pragma once

#include <Windows.h>

namespace qingying {

inline constexpr UINT TrayMenuAutostartCommandId = 40001;
inline constexpr UINT TrayMenuExitCommandId = 40002;
inline constexpr UINT TrayMenuAutomationCommandId = 40003;
inline constexpr wchar_t TrayMenuAutomationText[] = L"允许本机 Agent 接口";
inline constexpr wchar_t TrayMenuAutostartText[] = L"开机自启";
inline constexpr wchar_t TrayMenuExitText[] = L"退出";
inline constexpr wchar_t TrayMenuFontFace[] = L"Microsoft YaHei UI";
inline constexpr int TrayMenuFontSizePx = 14;
inline constexpr int TrayMenuDefaultDpi = 96;
inline constexpr int TrayMenuPadX = 20;
inline constexpr int TrayMenuPadY = 8;
inline constexpr int TrayMenuCheckColumnPx = 22;
inline constexpr int TrayMenuSeparatorHeightPx = 9;
inline constexpr int TrayMenuSeparatorInsetPx = 12;
inline constexpr int TrayMenuSeparatorThicknessPx = 1;
inline constexpr int TrayMenuMinItemHeightPx = 30;
inline constexpr int TrayMenuCheckStrokePx = 2;
inline constexpr COLORREF TrayMenuTextColor = RGB(55, 59, 66);
inline constexpr COLORREF TrayMenuHoverFill = RGB(245, 247, 249);
inline constexpr COLORREF TrayMenuFill = RGB(255, 255, 255);
inline constexpr COLORREF TrayMenuSeparatorColor = RGB(226, 229, 234);
inline constexpr int TrayMenuHoverInsetX = 6;
inline constexpr int TrayMenuHoverInsetY = 3;
inline constexpr int TrayMenuHoverRadius = 6;
inline constexpr int TrayMenuHoverShadowOffsetX = 2;
inline constexpr int TrayMenuHoverShadowOffsetY = 3;
inline constexpr int TrayMenuHoverShadowLayers = 5;
inline constexpr int TrayMenuHoverShadowSpreadPx = 1;
inline constexpr COLORREF TrayMenuHoverCardFill = RGB(255, 255, 255);
inline constexpr COLORREF TrayMenuHoverCardBorder = RGB(226, 229, 234);
inline constexpr COLORREF TrayMenuHoverShadow = RGB(148, 152, 158);

enum class TrayMenuItemKind : ULONG_PTR
{
  None = 0,
  Autostart = 1,
  Separator = 2,
  Exit = 3,
  Automation = 4,
};

inline int trayMenuScalePx(int px, int dpi)
{
  int safe_dpi = dpi;
  if (safe_dpi <= 0)
  {
    safe_dpi = TrayMenuDefaultDpi;
  }
  return MulDiv(px, safe_dpi, TrayMenuDefaultDpi);
}

inline const wchar_t* trayMenuLabelForId(UINT menu_id)
{
  if (menu_id == TrayMenuAutomationCommandId) return TrayMenuAutomationText;
  if (menu_id == TrayMenuAutostartCommandId)
  {
    return TrayMenuAutostartText;
  }
  if (menu_id == TrayMenuExitCommandId)
  {
    return TrayMenuExitText;
  }
  return L"";
}

inline TrayMenuItemKind trayMenuKindFromId(UINT menu_id)
{
  if (menu_id == TrayMenuAutomationCommandId) return TrayMenuItemKind::Automation;
  if (menu_id == TrayMenuAutostartCommandId)
  {
    return TrayMenuItemKind::Autostart;
  }
  if (menu_id == TrayMenuExitCommandId)
  {
    return TrayMenuItemKind::Exit;
  }
  return TrayMenuItemKind::None;
}

inline const wchar_t* trayMenuLabelForKind(TrayMenuItemKind kind)
{
  switch (kind)
  {
    case TrayMenuItemKind::Automation:
      return TrayMenuAutomationText;
    case TrayMenuItemKind::Autostart:
      return TrayMenuAutostartText;
    case TrayMenuItemKind::Exit:
      return TrayMenuExitText;
    case TrayMenuItemKind::Separator:
    case TrayMenuItemKind::None:
      break;
  }
  return L"";
}

inline LOGFONTW trayMenuLogFont(int dpi)
{
  LOGFONTW font = {};
  font.lfHeight = -trayMenuScalePx(TrayMenuFontSizePx, dpi);
  font.lfWeight = FW_NORMAL;
  font.lfCharSet = DEFAULT_CHARSET;
  font.lfOutPrecision = OUT_TT_PRECIS;
  font.lfClipPrecision = CLIP_DEFAULT_PRECIS;
  font.lfQuality = CLEARTYPE_QUALITY;
  font.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
  const errno_t copied = wcsncpy_s(font.lfFaceName, TrayMenuFontFace, _TRUNCATE);
  if (copied != 0 && copied != STRUNCATE)
  {
    font.lfFaceName[0] = L'\0';
  }
  return font;
}

inline int trayMenuItemHeightPx(int dpi)
{
  const int height = trayMenuScalePx(TrayMenuFontSizePx + TrayMenuPadY * 2, dpi);
  const int min_height = trayMenuScalePx(TrayMenuMinItemHeightPx, dpi);
  if (height < min_height)
  {
    return min_height;
  }
  return height;
}

inline int trayMenuMeasureHeight(ULONG_PTR item_data, int dpi)
{
  if (item_data == static_cast<ULONG_PTR>(TrayMenuItemKind::Separator))
  {
    return trayMenuScalePx(TrayMenuSeparatorHeightPx, dpi);
  }
  return trayMenuItemHeightPx(dpi);
}

inline RECT trayMenuHoverCardRect(const RECT& item, int dpi)
{
  RECT card = item;
  const int inset_x = trayMenuScalePx(TrayMenuHoverInsetX, dpi);
  const int inset_y = trayMenuScalePx(TrayMenuHoverInsetY, dpi);
  const int shadow_x = trayMenuScalePx(TrayMenuHoverShadowOffsetX, dpi);
  const int shadow_y = trayMenuScalePx(TrayMenuHoverShadowOffsetY, dpi);
  card.left += inset_x;
  card.top += inset_y;
  card.right -= inset_x + shadow_x;
  card.bottom -= inset_y + shadow_y;
  if (card.right <= card.left || card.bottom <= card.top)
  {
    return item;
  }
  return card;
}

inline int trayMenuRed(COLORREF color)
{
  return static_cast<int>(color & 0xFFu);
}

inline int trayMenuGreen(COLORREF color)
{
  return static_cast<int>((color >> 8) & 0xFFu);
}

inline int trayMenuBlue(COLORREF color)
{
  return static_cast<int>((color >> 16) & 0xFFu);
}

inline COLORREF trayMenuHoverShadowLayerColor(int layer_from_outside)
{
  int layer = layer_from_outside;
  if (layer < 1)
  {
    layer = 1;
  }
  if (layer > TrayMenuHoverShadowLayers)
  {
    layer = TrayMenuHoverShadowLayers;
  }
  const int lighten = TrayMenuHoverShadowLayers - layer;
  const int r =
      trayMenuRed(TrayMenuHoverShadow) +
      (trayMenuRed(TrayMenuFill) - trayMenuRed(TrayMenuHoverShadow)) * lighten /
          TrayMenuHoverShadowLayers;
  const int g =
      trayMenuGreen(TrayMenuHoverShadow) +
      (trayMenuGreen(TrayMenuFill) - trayMenuGreen(TrayMenuHoverShadow)) *
          lighten / TrayMenuHoverShadowLayers;
  const int b =
      trayMenuBlue(TrayMenuHoverShadow) +
      (trayMenuBlue(TrayMenuFill) - trayMenuBlue(TrayMenuHoverShadow)) * lighten /
          TrayMenuHoverShadowLayers;
  return RGB(r, g, b);
}

inline void fillTrayMenuItem(MENUITEMINFOW& info, UINT command_id, bool checked)
{
  info = {};
  info.cbSize = sizeof(MENUITEMINFOW);
  info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE;
  info.fType = MFT_OWNERDRAW;
  info.wID = command_id;
  info.dwItemData = static_cast<ULONG_PTR>(trayMenuKindFromId(command_id));
  info.fState = checked ? MFS_CHECKED : MFS_UNCHECKED;
}

inline void fillTrayMenuSeparator(MENUITEMINFOW& info)
{
  info = {};
  info.cbSize = sizeof(MENUITEMINFOW);
  info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_ID;
  info.fType = MFT_OWNERDRAW | MFT_SEPARATOR;
  info.wID = 0;
  info.dwItemData = static_cast<ULONG_PTR>(TrayMenuItemKind::Separator);
}

}  // namespace qingying
