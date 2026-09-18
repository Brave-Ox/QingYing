#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/action_result.hpp"

#include <Windows.h>

#include <cstdint>

namespace qingying {

// LPARAM/WPARAM carry only this numeric token. The corresponding payload is
// owned by UiMessageChannel until the UI thread takes or drains it.
using UiMessageToken = std::uintptr_t;

constexpr UiMessageToken kInvalidUiMessageToken = 0;
static_assert(sizeof(UiMessageToken) <= sizeof(LPARAM),
              "UI message tokens must fit in LPARAM");

// All application and overlay messages use one process-wide WM_APP range.
constexpr UINT WM_QINGYING_SMART_REGION_COMPLETE = WM_APP + 15;
constexpr UINT WM_QINGYING_CAPTURE_COMPLETE = WM_APP + 13;
constexpr UINT WM_QINGYING_RESULT_ACTION_COMPLETE = WM_APP + 14;

constexpr UINT WM_QINGYING_TRAY = WM_APP + 1;
// Posted from WM_HOTKEY handler to run capture on the UI thread.
constexpr UINT WM_QINGYING_BEGIN_CAPTURE = WM_APP + 2;
constexpr UINT WM_QINGYING_LONGSHOT_COMPLETE = WM_APP + 3;
// Posted by non-modal overlays after their window has delivered a result.
constexpr UINT WM_QINGYING_WORKFLOW_CONTINUE = WM_APP + 4;
constexpr UINT WM_QINGYING_SELECTION_OVERLAY_READY = WM_APP + 5;
constexpr UINT WM_QINGYING_SELECTION_LONGSHOT_PREVIEW = WM_APP + 6;
constexpr UINT WM_QINGYING_SELECTION_LONGSHOT_FINISHED = WM_APP + 7;
constexpr UINT WM_QINGYING_SELECTION_OVERLAY_ABORT = WM_APP + 8;
constexpr UINT WM_QINGYING_ANNOTATION_CANCEL = WM_APP + 9;
constexpr UINT WM_QINGYING_AUTOMATION_REQUEST = WM_APP + 10;
constexpr UINT WM_QINGYING_AUTOMATION_COMPLETE = WM_APP + 11;
constexpr UINT WM_QINGYING_AUTOMATION_WAKE = WM_APP + 13;
// Read-only diagnostic query. Returns 1 only after the first layered frame
// has been successfully committed through UpdateLayeredWindow; it never
// changes Overlay state.
constexpr UINT WM_QINGYING_SELECTION_OVERLAY_FIRST_FRAME_QUERY = WM_APP + 12;

struct AutomationRequestMessage {};
struct AutomationCompletionMessage {
  UiMessageToken ticket{kInvalidUiMessageToken};
};

struct LongShotCompletionMessage {
  ActionResult result;
  Image image;
};

struct SelectionOverlayLongShotPreviewMessage {
  Image image;
  std::chrono::steady_clock::time_point created_at{std::chrono::steady_clock::now()};
};

struct SelectionOverlayLongShotFinishedMessage {
  bool success{false};
};

namespace HotkeyIds {
constexpr int kCapture = 1;
}  // namespace HotkeyIds

namespace HotkeyDefaults {
constexpr UINT kCaptureModifiers = MOD_CONTROL | MOD_SHIFT;
constexpr UINT kCaptureVirtualKey = static_cast<UINT>('Q');
}  // namespace HotkeyDefaults

}  // namespace qingying
