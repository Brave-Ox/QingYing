#include "visual_frame_settler.hpp"

namespace qingying {
namespace longshot_detail {
namespace {

ImageStitchOptions stabilityOptions() {
  ImageStitchOptions options;
  options.min_overlap_rows = 16;
  options.right_edge_exclusion_pixels = 12;
  options.channel_tolerance = 2;
  options.minimum_match_per_mille = 960;
  options.require_overlap = true;
  return options;
}

bool isFatalOverlapRejection(OverlapRejectReason reason) noexcept {
  return reason == OverlapRejectReason::InvalidImage ||
         reason == OverlapRejectReason::WidthMismatch ||
         reason == OverlapRejectReason::InvalidEdgeExclusion;
}

}  // namespace

VisualFrameSettler::VisualFrameSettler() : stitcher_(stabilityOptions()) {}

VisualFrameSettler::VisualFrameSettler(ImageStitchOptions options)
    : stitcher_(options) {}

VisualFrameObservation VisualFrameSettler::observe(
    const Image& accumulated, const Image& sample) {
  ++sample_count_;
  OverlapEvidence evidence;
  if (!stitcher_.findOverlap(accumulated, sample, evidence)) {
    movement_overlap_ = 0;
    stable_movement_samples_ = 0;
    unchanged_samples_ = 0;
    return {isFatalOverlapRejection(evidence.reject_reason)
                ? VisualFrameDecision::FatalOverlap
                : VisualFrameDecision::RetryableOverlap,
            0, 0, 0,
            evidence.reject_reason};
  }
  if (!evidence.accepted()) {
    // A flat viewport is unsafe to stitch because every displacement looks
    // plausible. It can still safely establish end-of-content when the best
    // candidate is the complete, unchanged frame for three bounded samples.
    if (evidence.reject_reason == OverlapRejectReason::InsufficientTexture &&
        evidence.displacement_rows == 0) {
      ++unchanged_samples_;
      movement_overlap_ = 0;
      stable_movement_samples_ = 0;
      return {unchanged_samples_ >= 3 ? VisualFrameDecision::NoProgress
                                      : VisualFrameDecision::ObserveMore,
              evidence.candidate_overlap_rows, 0, accumulated.height,
              evidence.reject_reason};
    }
    movement_overlap_ = 0;
    stable_movement_samples_ = 0;
    unchanged_samples_ = 0;
    return {isFatalOverlapRejection(evidence.reject_reason)
                ? VisualFrameDecision::FatalOverlap
                : VisualFrameDecision::RetryableOverlap,
            0, 0, 0,
            evidence.reject_reason};
  }
  const int overlap_rows = evidence.overlap_rows;

  if (evidence.displacement_rows == 0) {
    ++unchanged_samples_;
    movement_overlap_ = 0;
    stable_movement_samples_ = 0;
    return {unchanged_samples_ >= 3 ? VisualFrameDecision::NoProgress
                                    : VisualFrameDecision::ObserveMore,
            overlap_rows, 0, accumulated.height};
  }

  unchanged_samples_ = 0;
  if (movement_overlap_ == overlap_rows) {
    ++stable_movement_samples_;
  } else {
    stable_movement_samples_ = 1;
  }
  movement_overlap_ = overlap_rows;

  return {stable_movement_samples_ >= 2
              ? VisualFrameDecision::StableMovement
              : VisualFrameDecision::ObserveMore,
          overlap_rows, evidence.displacement_rows, evidence.output_rows};
}

int VisualFrameSettler::sampleCount() const noexcept { return sample_count_; }

}  // namespace longshot_detail
}  // namespace qingying
