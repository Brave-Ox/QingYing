#pragma once

#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/overlay/selection_overlay.hpp"

#include <Windows.h>

#include <cstdint>

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

// Keep HWND in the application/UI boundary and convert only at the long-shot
// request boundary, whose integer field is also the plugin ABI representation.
inline LongShotRequest makeLongShotRequest(HWND owner_window,
                                            const SelectionResult& selection) {
  return makeLongShotRequest(reinterpret_cast<std::uintptr_t>(owner_window),
                             selection);
}

}  // namespace qingying
