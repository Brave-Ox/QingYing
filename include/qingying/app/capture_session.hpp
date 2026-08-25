#pragma once

#include "qingying/action/image.hpp"

#include <utility>

namespace qingying {

// Holds the latest capture result (the Image) between CaptureRegion and
// Copy/Save handlers. Cleared before a new capture begins.
class CaptureSession {
 public:
  bool hasResult() const { return has_result_; }
  const Image& result() const { return image_; }

  void setResult(Image image) {
    image_ = std::move(image);
    has_result_ = true;
  }
  void clear() {
    image_ = Image{};
    has_result_ = false;
  }

 private:
  Image image_;
  bool has_result_{false};
};

}  // namespace qingying
