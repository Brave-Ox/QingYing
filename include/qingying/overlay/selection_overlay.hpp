#pragma once

#include "qingying/action/image.hpp"

#include <functional>

namespace qingying {

enum class SelectionAction {
  None,
  Copy,
  Save,
  Edit,
  Pin,
  LongShot,
};

struct SelectionResult {
  bool cancelled{true};
  int x{0};
  int y{0};
  int width{0};
  int height{0};
  SelectionAction action{SelectionAction::None};
};

using SelectionCallback = std::function<void(const SelectionResult&)>;

class SelectionOverlay {
 public:
  // Show fullscreen mask; after selection, display an action toolbar near the
  // region and invoke the callback when the user chooses an action or cancels.
  // `background` is the desktop snapshot (physical pixels) shown inside the
  // overlay so other windows (incl. owned popups) stay visible in the mask UI;
  // an empty image falls back to a pure translucent mask.
  // Returns false if overlay could not be shown.
  bool show(const Image& background, SelectionCallback callback);

  void hide();
};

}  // namespace qingying
