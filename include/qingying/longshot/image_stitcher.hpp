#pragma once

#include "qingying/action/image.hpp"

#include <cstdint>

namespace qingying {

// Options for matching the bottom of an accumulated image with the top of
// the next viewport frame.
struct ImageStitchOptions {
  int min_overlap_rows{1};
  int max_overlap_rows{0};  // 0 means no explicit upper bound.
  int sample_step{4};
  std::uint8_t channel_tolerance{0};
};

class ImageStitcher {
 public:
  explicit ImageStitcher(ImageStitchOptions options = {});

  // Returns true when the two images are compatible and the overlap search
  // completed. overlap_rows may be zero: adjacent frames do not have to
  // overlap for append() to succeed.
  bool findOverlap(const Image& accumulated, const Image& next,
                  int& overlap_rows) const;

  // Appends next below accumulated after removing the detected overlap. If
  // accumulated is empty, next becomes the first frame. When provided,
  // overlap_rows receives the number of removed rows.
  bool append(Image& accumulated, const Image& next,
              int* overlap_rows = nullptr) const;

 private:
  bool validImage(const Image& image) const;
  bool rowsMatch(const Image& accumulated, const Image& next,
                 int overlap_rows) const;
  bool pixelsMatch(std::uint32_t lhs, std::uint32_t rhs) const;

  ImageStitchOptions options_;
};

}  // namespace qingying
