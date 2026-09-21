#pragma once

#include <array>
#include <string>

#include <Windows.h>

namespace qingying {

inline constexpr UINT TrayMenuAutostartCommandId = 40001;
inline constexpr UINT TrayMenuExitCommandId = 40002;
inline constexpr UINT TrayMenuAutomationCommandId = 40003;
inline constexpr UINT TrayMenuCaptureCommandId = 40004;
inline constexpr UINT TrayMenuSettingsCommandId = 40005;
inline constexpr UINT TrayMenuCommandCommandId = 40006;
inline constexpr wchar_t TrayMenuCaptureText[] = L"开始截图";
inline constexpr wchar_t TrayMenuCommandText[] = L"口令...";
inline constexpr wchar_t TrayMenuSettingsText[] = L"设置...";
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
inline constexpr COLORREF TrayMenuTextColor = RGB(31, 35, 41);
inline constexpr COLORREF TrayMenuFill = RGB(255, 255, 255);
inline constexpr COLORREF TrayMenuSeparatorColor = RGB(236, 239, 244);
inline constexpr int TrayMenuHoverInsetX = 6;
inline constexpr int TrayMenuHoverInsetY = 3;
inline constexpr int TrayMenuHoverRadius = 6;
inline constexpr COLORREF TrayMenuHoverCardFill = RGB(238, 245, 255);
inline constexpr COLORREF TrayMenuHoverCardBorder = RGB(229, 236, 245);

enum class TrayMenuItemKind : ULONG_PTR
{
  None = 0,
  Autostart = 1,
  Separator = 2,
  Exit = 3,
  Automation = 4,
  Capture = 5,
  Settings = 6,
  Command = 7,
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
  if (menu_id == TrayMenuCaptureCommandId) return TrayMenuCaptureText;
  if (menu_id == TrayMenuCommandCommandId) return TrayMenuCommandText;
  if (menu_id == TrayMenuSettingsCommandId) return TrayMenuSettingsText;
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

inline const std::array<UINT, 6>& trayMenuCommandOrder()
{
  static constexpr std::array<UINT, 6> Order{
      TrayMenuCaptureCommandId,
      TrayMenuCommandCommandId,
      TrayMenuSettingsCommandId,
      TrayMenuAutostartCommandId,
      TrayMenuAutomationCommandId,
      TrayMenuExitCommandId};
  return Order;
}

inline std::wstring trayMenuCaptureDisplayText(const std::wstring& hotkey)
{
  if (hotkey.empty())
  {
    return TrayMenuCaptureText;
  }
  return std::wstring(TrayMenuCaptureText) + L"\t" + hotkey;
}

inline TrayMenuItemKind trayMenuKindFromId(UINT menu_id)
{
  if (menu_id == TrayMenuCaptureCommandId) return TrayMenuItemKind::Capture;
  if (menu_id == TrayMenuCommandCommandId) return TrayMenuItemKind::Command;
  if (menu_id == TrayMenuSettingsCommandId) return TrayMenuItemKind::Settings;
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
    case TrayMenuItemKind::Capture:
      return TrayMenuCaptureText;
    case TrayMenuItemKind::Command:
      return TrayMenuCommandText;
    case TrayMenuItemKind::Settings:
      return TrayMenuSettingsText;
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
  card.left += inset_x;
  card.top += inset_y;
  card.right -= inset_x;
  card.bottom -= inset_y;
  if (card.right <= card.left || card.bottom <= card.top)
  {
    return item;
  }
  return card;
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
