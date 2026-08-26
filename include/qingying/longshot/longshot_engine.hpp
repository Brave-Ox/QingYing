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

// Step-4 raw result: two captures of the exact same screen rectangle, before
// and after one scroll input. It is not yet the final stitched long image.
struct LongShotFramePair {
  Image first_frame;
  Image second_frame;

  bool valid() const;
  void clear();
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

  // Captures exactly two raw frames around one wheel input. This staged API
  // keeps captureSelection's final-image contract intact until stitching is
  // connected in the next step.
  ActionResult captureInitialPair(const LongShotRequest& request,
                                  LongShotFramePair& out);

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace qingying
