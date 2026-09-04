#include "qingying/longshot/longshot_profile.hpp"

#include <cstdint>

namespace qingying {

bool LongShotProfileResult::valid() const {
  return scroll_target != 0 && content.valid();
}

bool LongShotProfileResult::containsSelection(
    const ScreenPhysicalRect& selection) const {
  if (!valid() || selection.empty()) {
    return false;
  }

  const std::int64_t selection_left = selection.x;
  const std::int64_t selection_top = selection.y;
  const std::int64_t selection_right =
      selection_left + static_cast<std::int64_t>(selection.width);
  const std::int64_t selection_bottom =
      selection_top + static_cast<std::int64_t>(selection.height);
  const std::int64_t content_left = content.x;
  const std::int64_t content_top = content.y;
  const std::int64_t content_right =
      content_left + static_cast<std::int64_t>(content.width);
  const std::int64_t content_bottom =
      content_top + static_cast<std::int64_t>(content.height);

  return selection_left >= content_left && selection_top >= content_top &&
         selection_right <= content_right &&
         selection_bottom <= content_bottom;
}

bool LongShotProfileResult::containsSelection(int x, int y, int width,
                                              int height) const {
  return containsSelection(ScreenPhysicalRect{x, y, width, height});
}

}  // qingying 命名空间
