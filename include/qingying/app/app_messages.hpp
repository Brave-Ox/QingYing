#pragma once

#include <Windows.h>

namespace qingying {

// Posted from WM_HOTKEY handler to run capture on the UI thread.
constexpr UINT WM_QINGYING_BEGIN_CAPTURE = WM_APP + 2;
constexpr UINT WM_QINGYING_LONGSHOT_COMPLETE = WM_APP + 3;
// Posted by non-modal overlays after their window has delivered a result.
constexpr UINT WM_QINGYING_WORKFLOW_CONTINUE = WM_APP + 4;
constexpr UINT WM_QINGYING_BROWSER_IMAGE = WM_APP + 5;

namespace HotkeyIds {
constexpr int kCapture = 1;
}  // namespace HotkeyIds

namespace HotkeyDefaults {
constexpr UINT kCaptureModifiers = MOD_CONTROL | MOD_SHIFT;
constexpr UINT kCaptureVirtualKey = static_cast<UINT>('Q');
}  // namespace HotkeyDefaults

}  // namespace qingying
