#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"

#include <cstdint>

namespace qingying {

class CaptureEngine;

// SelectionOverlay supplies this request through the Application layer. The
// screen rectangle is authoritative: an application profile may validate it
// and find a scroll target, but must not replace it with a whole-window rect.
struct LongShotRequest {
  std::uintptr_t owner_window{0};
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  bool valid() const {
    return owner_window != 0 && width > 0 && height > 0;
  }
};

class LongShotEngine {
 public:
  explicit LongShotEngine(CaptureEngine& capture);
  ~LongShotEngine();

  LongShotEngine(const LongShotEngine&) = delete;
  LongShotEngine& operator=(const LongShotEngine&) = delete;

  // Repeatedly captures exactly request.{x,y,width,height} while scrolling
  // request.owner_window through an application-specific profile.
  ActionResult captureSelection(const LongShotRequest& request, Image& out);

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace qingying
