#pragma once

namespace qingying {

class SelectionOverlay {
 public:
  // Show fullscreen mask; on region done, caller dispatches CaptureRegion.
  bool show();
  void hide();
};

}  // namespace qingying
