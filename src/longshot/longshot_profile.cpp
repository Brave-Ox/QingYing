#include "qingying/longshot/longshot_profile.hpp"

#include <cstdint>

namespace qingying {

bool LongShotProfileResult::valid() const {
  return scroll_target != 0 && content_width > 0 && content_height > 0;
}

bool LongShotProfileResult::containsSelection(int x, int y, int width,
                                              int height) const {
  if (!valid() || width <= 0 || height <= 0) {
    return false;
  }

  const std::int64_t selection_left = x;
  const std::int64_t selection_top = y;
  const std::int64_t selection_right =
      selection_left + static_cast<std::int64_t>(width);
  const std::int64_t selection_bottom =
      selection_top + static_cast<std::int64_t>(height);
  const std::int64_t content_left = content_x;
  const std::int64_t content_top = content_y;
  const std::int64_t content_right =
      content_left + static_cast<std::int64_t>(content_width);
  const std::int64_t content_bottom =
      content_top + static_cast<std::int64_t>(content_height);

  return selection_left >= content_left && selection_top >= content_top &&
         selection_right <= content_right &&
         selection_bottom <= content_bottom;
}

}  // qingying 命名空间
