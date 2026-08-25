#pragma once

#include <Windows.h>

namespace qingying {

// Posted from WM_HOTKEY handler to run capture on the UI thread.
constexpr UINT WM_QINGYING_BEGIN_CAPTURE = WM_APP + 2;

namespace HotkeyIds {
constexpr int kCapture = 1;
}  // namespace HotkeyIds

namespace HotkeyDefaults {
constexpr UINT kCaptureModifiers = MOD_CONTROL | MOD_SHIFT;
constexpr UINT kCaptureVirtualKey = static_cast<UINT>('Q');
}  // namespace HotkeyDefaults

}  // namespace qingying
