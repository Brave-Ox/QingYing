#include "qingying/app/capture_preview.hpp"

#include <algorithm>
#include <cstddef>

namespace qingying {

namespace {

bool imageStorageIsValid(const Image& image) {
  if (image.empty()) {
    return false;
  }
  const std::size_t expected = static_cast<std::size_t>(image.width) *
                               static_cast<std::size_t>(image.height);
  return image.pixels.size() == expected;
}

}  // namespace

bool composeCapturePreview(Image& background, const Image& selection_image,
                           const ScreenPhysicalRect& selection,
                           const coord::VirtualScreenRect& screen) {
  if (!imageStorageIsValid(background) ||
      !imageStorageIsValid(selection_image) ||
      selection.empty() ||
      selection_image.width != selection.width ||
      selection_image.height != selection.height ||
      background.width != screen.width || background.height != screen.height) {
    return false;
  }

  const int copy_width =
      (std::min)(selection.width, selection_image.width);
  const int copy_height =
      (std::min)(selection.height, selection_image.height);
  const ImagePixelRect destination =
      coord::screenToImage(selection, screen);
  for (int source_y = 0; source_y < copy_height; ++source_y) {
    const int target_y = destination.y + source_y;
    if (target_y < 0 || target_y >= background.height) {
      continue;
    }
    for (int source_x = 0; source_x < copy_width; ++source_x) {
      const int target_x = destination.x + source_x;
      if (target_x < 0 || target_x >= background.width) {
        continue;
      }
      background.pixels[static_cast<std::size_t>(target_y) *
                            static_cast<std::size_t>(background.width) +
                        static_cast<std::size_t>(target_x)] =
          selection_image
              .pixels[static_cast<std::size_t>(source_y) *
                          static_cast<std::size_t>(selection_image.width) +
                      static_cast<std::size_t>(source_x)];
    }
  }
  return true;
}

}  // namespace qingying
