#pragma once

#include "qingying/longshot/image_stitcher.hpp"

namespace qingying {
namespace longshot_detail {

enum class VisualFrameDecision {
  ObserveMore,
  StableMovement,
  NoProgress,
  RetryableOverlap,
  FatalOverlap,
};

struct VisualFrameObservation {
  VisualFrameDecision decision{VisualFrameDecision::ObserveMore};
  int overlap_rows{0};
  int displacement_rows{0};
  int output_rows{0};
  OverlapRejectReason reject_reason{OverlapRejectReason::None};
};

struct VisualFrameSettlingPolicy {
  int displacement_tolerance_rows{2};
  int required_movement_samples{2};
  int required_no_progress_samples{3};
};

// Classifies captures made after one scroll input. Movement is accepted only
// after two compatible samples; unchanged content requires three samples so a
// delayed repaint is not mistaken for the end of the document.
class VisualFrameSettler {
 public:
  VisualFrameSettler();
  explicit VisualFrameSettler(ImageStitchOptions options);
  VisualFrameSettler(ImageStitchOptions options,
                     VisualFrameSettlingPolicy policy);

  VisualFrameObservation observe(const Image& accumulated,
                                 const Image& sample);
  int sampleCount() const noexcept;

 private:
  ImageStitcher stitcher_;
  VisualFrameSettlingPolicy policy_;
  int movement_displacement_rows_{0};
  std::uint16_t movement_best_score_per_mille_{0};
  int stable_movement_samples_{0};
  int unchanged_samples_{0};
  int sample_count_{0};
};

}  // namespace longshot_detail
}  // namespace qingying
