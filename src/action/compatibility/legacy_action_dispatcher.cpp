#include "qingying/action/compatibility/legacy_action_dispatcher.h"

namespace qingying {

std::optional<ActionRequest> adaptLegacyActionRequest(
    const LegacyActionRequest& legacy) {
  switch (legacy.type) {
    case ActionType::Status:
      return makeActionRequest(StatusRequest{});
    case ActionType::CaptureRegion:
      return makeActionRequest(CaptureRegionRequest{
          ScreenPhysicalRect{legacy.x, legacy.y, legacy.width, legacy.height}});
    case ActionType::CaptureWindow:
      return makeActionRequest(CaptureWindowRequest{legacy.window_query});
    case ActionType::CropCenter:
      return makeActionRequest(CropCenterRequest{legacy.crop_w, legacy.crop_h});
    case ActionType::Copy:
      return makeActionRequest(CopyRequest{ResultSelection::current()});
    case ActionType::Save:
      return makeActionRequest(
          SaveRequest{ResultSelection::current(), legacy.save_path});
    case ActionType::Pin:
      return makeActionRequest(PinRequest{ResultSelection::current()});
    case ActionType::LongShotRegion:
    case ActionType::SuggestName:
      // These actions do not have a stable external payload yet. Long-shot
      // continues to use LongShotRequest until its async contract is frozen.
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace qingying
