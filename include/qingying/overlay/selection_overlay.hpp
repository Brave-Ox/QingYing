#pragma once

#include <functional>

namespace qingying {

struct SelectionResult {
  bool cancelled{true};
  int x{0};
  int y{0};
  int width{0};
  int height{0};
};

using SelectionCallback = std::function<void(const SelectionResult&)>;

class SelectionOverlay {
 public:
  // Show fullscreen mask; invoke callback when region is confirmed or cancelled.
  // Returns false if overlay could not be shown (not implemented yet).
  bool show(SelectionCallback callback);

  void hide();
};

}  // namespace qingying
