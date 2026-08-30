#pragma once

#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/overlay/selection_overlay.hpp"

namespace qingying {

// Converts the screen-space selection produced by SelectionOverlay into the
// long-shot contract. A cancelled selection must never leave a usable request.
inline LongShotRequest makeLongShotRequest(
    std::uintptr_t owner_window, const SelectionResult& selection) {
  if (selection.cancelled) {
    return LongShotRequest{};
  }

  return LongShotRequest{owner_window, selection.x, selection.y,
                         selection.width, selection.height};
}

}  // namespace qingying
