#include "qingying/longshot/image_stitcher.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

namespace qingying {
namespace {

std::uint8_t channel(std::uint32_t pixel, int shift) {
  return static_cast<std::uint8_t>((pixel >> shift) & 0xFFu);
}

bool withinTolerance(std::uint8_t lhs, std::uint8_t rhs,
                     std::uint8_t tolerance) {
  const int difference = static_cast<int>(lhs) - static_cast<int>(rhs);
  return std::abs(difference) <= static_cast<int>(tolerance);
}

}  // namespace

ImageStitcher::ImageStitcher(ImageStitchOptions options)
    : options_(options) {}

bool ImageStitcher::validImage(const Image& image) const {
  if (image.width <= 0 || image.height <= 0) {
    return false;
  }

  const std::size_t width = static_cast<std::size_t>(image.width);
  const std::size_t height = static_cast<std::size_t>(image.height);
  if (height > std::numeric_limits<std::size_t>::max() / width) {
    return false;
  }
  return image.pixels.size() == width * height;
}

bool ImageStitcher::pixelsMatch(std::uint32_t lhs, std::uint32_t rhs) const {
  return withinTolerance(channel(lhs, 0), channel(rhs, 0),
                         options_.channel_tolerance) &&
         withinTolerance(channel(lhs, 8), channel(rhs, 8),
                         options_.channel_tolerance) &&
         withinTolerance(channel(lhs, 16), channel(rhs, 16),
                         options_.channel_tolerance) &&
         withinTolerance(channel(lhs, 24), channel(rhs, 24),
                         options_.channel_tolerance);
}

bool ImageStitcher::rowsMatch(const Image& accumulated, const Image& next,
                              int overlap_rows) const {
  const int step = std::max(1, options_.sample_step);
  const int width = accumulated.width;

  const auto pixelAt = [](const Image& image, int x, int y) {
    return image.pixels[static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(image.width) +
                        static_cast<std::size_t>(x)];
  };

  for (int y = 0; y < overlap_rows; y += step) {
    const int accumulated_y = accumulated.height - overlap_rows + y;
    for (int x = 0; x < width; x += step) {
      if (!pixelsMatch(pixelAt(accumulated, x, accumulated_y),
                       pixelAt(next, x, y))) {
        return false;
      }
    }

    // Always include the right edge of each sampled row.
    if ((width - 1) % step != 0 &&
        !pixelsMatch(pixelAt(accumulated, width - 1, accumulated_y),
                     pixelAt(next, width - 1, y))) {
      return false;
    }
  }

  // Always include the last overlap row when it was skipped by the step.
  if ((overlap_rows - 1) % step != 0) {
    const int accumulated_y = accumulated.height - 1;
    const int next_y = overlap_rows - 1;
    for (int x = 0; x < width; x += step) {
      if (!pixelsMatch(pixelAt(accumulated, x, accumulated_y),
                       pixelAt(next, x, next_y))) {
        return false;
      }
    }
    if ((width - 1) % step != 0 &&
        !pixelsMatch(pixelAt(accumulated, width - 1, accumulated_y),
                     pixelAt(next, width - 1, next_y))) {
      return false;
    }
  }

  return true;
}

bool ImageStitcher::findOverlap(const Image& accumulated, const Image& next,
                                int& overlap_rows) const {
  overlap_rows = 0;
  if (!validImage(accumulated) || !validImage(next) ||
      accumulated.width != next.width) {
    return false;
  }

  const int minimum = std::max(1, options_.min_overlap_rows);
  int maximum = std::min(accumulated.height, next.height);
  if (options_.max_overlap_rows > 0) {
    maximum = std::min(maximum, options_.max_overlap_rows);
  }
  if (minimum > maximum) {
    return true;
  }

  // Prefer the largest overlap. This avoids duplicating content when the
  // scroll step is smaller than one viewport.
  for (int candidate = maximum; candidate >= minimum; --candidate) {
    if (rowsMatch(accumulated, next, candidate)) {
      overlap_rows = candidate;
      break;
    }
  }
  return true;
}

bool ImageStitcher::append(Image& accumulated, const Image& next,
                           int* overlap_rows) const {
  if (overlap_rows != nullptr) {
    *overlap_rows = 0;
  }
  if (!validImage(next)) {
    return false;
  }
  if (accumulated.empty()) {
    accumulated = next;
    return true;
  }
  if (!validImage(accumulated) || accumulated.width != next.width) {
    return false;
  }

  int detected_overlap = 0;
  if (!findOverlap(accumulated, next, detected_overlap)) {
    return false;
  }

  const std::size_t width = static_cast<std::size_t>(next.width);
  const std::size_t accumulated_height =
      static_cast<std::size_t>(accumulated.height);
  const std::size_t next_height = static_cast<std::size_t>(next.height);
  const std::size_t new_rows = next_height -
                               static_cast<std::size_t>(detected_overlap);
  const std::size_t max_pixels_per_width =
      std::numeric_limits<std::size_t>::max() / width;
  if (new_rows > max_pixels_per_width ||
      accumulated_height > max_pixels_per_width - new_rows ||
      new_rows > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      accumulated_height >
          static_cast<std::size_t>(std::numeric_limits<int>::max()) -
              new_rows) {
    return false;
  }

  const std::size_t new_pixel_count =
      (accumulated_height + new_rows) * width;
  std::vector<std::uint32_t> merged;
  try {
    merged.reserve(new_pixel_count);
    merged.insert(merged.end(), accumulated.pixels.begin(),
                  accumulated.pixels.end());
    const std::size_t first_new_pixel =
        static_cast<std::size_t>(detected_overlap) * width;
    merged.insert(merged.end(), next.pixels.begin() +
                                    static_cast<std::ptrdiff_t>(first_new_pixel),
                  next.pixels.end());
  } catch (...) {
    return false;
  }

  accumulated.height = static_cast<int>(accumulated_height + new_rows);
  accumulated.pixels.swap(merged);
  if (overlap_rows != nullptr) {
    *overlap_rows = detected_overlap;
  }
  return true;
}

}  // namespace qingying
