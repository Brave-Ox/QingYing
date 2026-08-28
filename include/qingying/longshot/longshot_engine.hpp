#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"

#include <cstdint>
#include <functional>

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

// Safety limits for one long-shot capture. The initial pair already consumes
// two frames, so max_frames must be at least two.
struct LongShotLimits {
  int max_frames{30};
  int max_output_height{30000};

  bool valid() const {
    return max_frames >= 2 && max_output_height > 0;
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

using LongShotProgressCallback = std::function<void(const Image&)>;
using LongShotContinueCallback = std::function<bool()>;

class LongShotEngine {
 public:
  explicit LongShotEngine(CaptureEngine& capture, LongShotLimits limits = {});
  ~LongShotEngine();

  LongShotEngine(const LongShotEngine&) = delete;
  LongShotEngine& operator=(const LongShotEngine&) = delete;

  // Repeatedly captures exactly request.{x,y,width,height} while scrolling
  // request.owner_window through an application-specific profile. The loop
  // stops on a full-frame overlap or either safety limit.
  ActionResult captureSelection(const LongShotRequest& request, Image& out);

  // Interactive variant: reports the accumulated image after every frame and
  // stops cleanly when should_continue returns false.
  ActionResult captureSelection(const LongShotRequest& request, Image& out,
                                LongShotProgressCallback on_progress,
                                LongShotContinueCallback should_continue);

  // Captures exactly two raw frames around one wheel input. This staged API is
  // kept for validating the first scroll independently of the final loop.
  ActionResult captureInitialPair(const LongShotRequest& request,
                                  LongShotFramePair& out);

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace qingying
