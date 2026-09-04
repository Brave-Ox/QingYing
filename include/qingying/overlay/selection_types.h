#pragma once

#include "qingying/geometry/rect_types.h"

#include <functional>

namespace qingying {

// UI-level intent selected from the result toolbar. The geometry inherited
// from ScreenPhysicalRect is always in physical desktop coordinates.
enum class SelectionAction {
  None,
  Copy,
  Save,
  Edit,
  Pin,
  LongShot,
};

// Control messages sent from the long-shot overlay to its owner.
enum class LongShotControl {
  TogglePause,
  Stop,
};

struct SelectionIntent : ScreenPhysicalRect {
  bool cancelled{true};
  SelectionAction action{SelectionAction::None};

  constexpr bool valid() const noexcept {
    return !cancelled && ScreenPhysicalRect::valid();
  }

  constexpr ScreenPhysicalRect screenRect() const noexcept {
    return ScreenPhysicalRect{x, y, width, height};
  }
};

// Compatibility name retained while callers migrate to the explicit intent
// terminology. It no longer carries annotated_image; annotated output is a
// separate AnnotationFinishResult / ResultStore payload.
using SelectionResult = SelectionIntent;

using SelectionCallback = std::function<void(const SelectionIntent&)>;
using LongShotControlCallback = std::function<void(LongShotControl)>;

}  // namespace qingying
