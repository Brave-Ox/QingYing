#pragma once

#include "qingying/action/types.hpp"

namespace qingying {

// Compatibility-only request for callers that still build the old all-fields
// action object. New code must construct ActionRequest from one typed payload.
//
// Keep reason: migrate existing local/early integration callers without
// changing their source and preserve the old field-to-payload mapping while
// the typed ActionRequest contract settles.
// Replacement: ActionRequest plus makeActionRequest() and a typed payload.
// Removal condition: no supported caller includes this header and the
// compatibility regression tests can be deleted with the migration owner.
// Owner: action/automation maintainers; production targets must not depend on
// qingying_action_compatibility.
struct [[deprecated(
    "Use qingying::ActionRequest with a typed payload instead")]]
    LegacyActionRequest {
  ActionType type{ActionType::Status};

  // CaptureRegion / LongShotRegion, in physical screen pixels.
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  // LongShotRegion: target window recorded before SelectionOverlay takes
  // foreground focus. Kept as an integer so this compatibility header stays
  // free of Win32 headers.
  std::uintptr_t window_handle{0};

  // CaptureWindow
  std::wstring window_query;

  // CropCenter
  int crop_w{0};
  int crop_h{0};

  // Save
  std::wstring save_path;
};

}  // namespace qingying
