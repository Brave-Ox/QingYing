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

}  // namespace

VisualFrameSettler::VisualFrameSettler() : stitcher_(stabilityOptions()) {}

VisualFrameObservation VisualFrameSettler::observe(
    const Image& accumulated, const Image& sample) {
  ++sample_count_;
  int overlap_rows = 0;
  if (!stitcher_.findOverlap(accumulated, sample, overlap_rows) ||
      overlap_rows == 0) {
    return {VisualFrameDecision::LostOverlap, 0};
  }

  if (overlap_rows == sample.height) {
    ++unchanged_samples_;
    movement_overlap_ = 0;
    stable_movement_samples_ = 0;
    return {unchanged_samples_ >= 3 ? VisualFrameDecision::NoProgress
                                    : VisualFrameDecision::ObserveMore,
            overlap_rows};
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
          overlap_rows};
}

int VisualFrameSettler::sampleCount() const noexcept { return sample_count_; }

}  // namespace longshot_detail
}  // namespace qingying
