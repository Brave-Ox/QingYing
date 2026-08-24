#include "qingying/overlay/selection_overlay.hpp"

namespace qingying {

bool SelectionOverlay::show(SelectionCallback /*callback*/) {
  // Overlay UI is owned by qingying_overlay; integration wires the callback here.
  return false;
}

void SelectionOverlay::hide() {}

}  // namespace qingying
