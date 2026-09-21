#include "visual_frame_settler.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>

namespace qingying {
namespace longshot_detail {
namespace {

ImageStitchOptions stabilityOptions() {
  ImageStitchOptions options;
  options.min_overlap_rows = 16;
  options.side_exclusion.mode = SideExclusionMode::Automatic;
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

VisualFrameSettlingPolicy normalizedPolicy(
    VisualFrameSettlingPolicy policy) {
  policy.displacement_tolerance_rows =
      (std::max)(0, policy.displacement_tolerance_rows);
  policy.required_movement_samples =
      (std::max)(1, policy.required_movement_samples);
  policy.required_no_progress_samples =
      (std::max)(1, policy.required_no_progress_samples);
  return policy;
}

bool displacementFitsCluster(int candidate_rows, int cluster_rows,
                             int tolerance_rows) noexcept {
  return std::abs(static_cast<std::int64_t>(candidate_rows) -
                  static_cast<std::int64_t>(cluster_rows)) <= tolerance_rows;
}

}  // namespace

VisualFrameSettler::VisualFrameSettler()
    : VisualFrameSettler(stabilityOptions(), {}) {}

VisualFrameSettler::VisualFrameSettler(ImageStitchOptions options)
    : VisualFrameSettler(options, {}) {}

VisualFrameSettler::VisualFrameSettler(
    ImageStitchOptions options, VisualFrameSettlingPolicy policy)
    : stitcher_(options), policy_(normalizedPolicy(policy)) {}

VisualFrameObservation VisualFrameSettler::observe(
    const Image& accumulated, const Image& sample) {
  ++sample_count_;
  OverlapEvidence evidence;
  if (!stitcher_.findOverlap(accumulated, sample, evidence)) {
    movement_displacement_rows_ = 0;
    movement_best_score_per_mille_ = 0;
    stable_movement_samples_ = 0;
    unchanged_samples_ = 0;
    return {isFatalOverlapRejection(evidence.reject_reason)
                ? VisualFrameDecision::FatalOverlap
                : VisualFrameDecision::RetryableOverlap,
            0, 0, 0,
            evidence.reject_reason};
  }
  if (!evidence.accepted()) {
    movement_displacement_rows_ = 0;
    movement_best_score_per_mille_ = 0;
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
    movement_displacement_rows_ = 0;
    movement_best_score_per_mille_ = 0;
    stable_movement_samples_ = 0;
    return {unchanged_samples_ >= policy_.required_no_progress_samples
                ? VisualFrameDecision::NoProgress
                : VisualFrameDecision::ObserveMore,
            overlap_rows, 0, accumulated.height};
  }

  unchanged_samples_ = 0;
  if (stable_movement_samples_ > 0 &&
      displacementFitsCluster(evidence.displacement_rows,
                              movement_displacement_rows_,
                              policy_.displacement_tolerance_rows) &&
      evidence.selected_score_per_mille >=
          movement_best_score_per_mille_) {
    ++stable_movement_samples_;
    movement_best_score_per_mille_ = evidence.selected_score_per_mille;
  } else {
    stable_movement_samples_ = 1;
    movement_displacement_rows_ = evidence.displacement_rows;
    movement_best_score_per_mille_ = evidence.selected_score_per_mille;
  }

  return {stable_movement_samples_ >= policy_.required_movement_samples
              ? VisualFrameDecision::StableMovement
              : VisualFrameDecision::ObserveMore,
          overlap_rows, evidence.displacement_rows, evidence.output_rows};
}

int VisualFrameSettler::sampleCount() const noexcept { return sample_count_; }

}  // namespace longshot_detail
}  // namespace qingying
