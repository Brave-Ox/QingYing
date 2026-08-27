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
};

struct SelectionResult {
  bool cancelled{true};
  int x{0};
  int y{0};
  int width{0};
  int height{0};
  SelectionAction action{SelectionAction::None};
  // 就地标注确认后带回合成图；其它动作保持 empty。
  Image annotated_image;
};

using SelectionCallback = std::function<void(const SelectionResult&)>;

class SelectionOverlay {
 public:
  // Show fullscreen mask; after selection, display an action toolbar near the
  // region and invoke the callback when the user chooses an action or cancels.
  // Returns false if overlay could not be shown (not implemented yet).
  bool show(SelectionCallback callback);

  void hide();
};

}  // namespace qingying
