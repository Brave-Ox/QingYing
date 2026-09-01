#pragma once

#include <Windows.h>
#include <Shellapi.h>

namespace qingying {

inline constexpr wchar_t TrayIconTipText[] = L"轻映";
inline constexpr UINT TrayIconId = 1;
inline constexpr UINT TrayNotifyIconFlags =
    NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;

// Win7+ 在 NOTIFYICON_VERSION_4 下必须带 NIF_SHOWTIP，否则悬停不出现标准 tip。
inline void fillTrayNotifyIconData(NOTIFYICONDATAW& nid, HWND hwnd,
                                   UINT callback_message, HICON icon)
{
  nid = {};
  nid.cbSize = sizeof(NOTIFYICONDATAW);
  nid.hWnd = hwnd;
  nid.uID = TrayIconId;
  nid.uFlags = TrayNotifyIconFlags;
  nid.uCallbackMessage = callback_message;
  nid.hIcon = icon;
  const errno_t copied = wcsncpy_s(nid.szTip, TrayIconTipText, _TRUNCATE);
  if (copied != 0 && copied != STRUNCATE)
  {
    nid.szTip[0] = L'\0';
  }
}

}  // namespace qingying
