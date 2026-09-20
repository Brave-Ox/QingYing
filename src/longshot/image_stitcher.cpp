#include "qingying/longshot/image_stitcher.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <utility>

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

SideExclusionRange resolveSideExclusion(
    int image_width, const SideExclusionPolicy& policy) noexcept {
  SideExclusionRange range;
  if (image_width <= 0) return range;

  if (policy.mode == SideExclusionMode::Automatic) {
    if (policy.minimum_center_pixels <= 0 ||
        policy.automatic_minimum_pixels < 0 ||
        policy.automatic_width_divisor <= 0 ||
        policy.automatic_maximum_width_divisor <= 0) {
      return range;
    }
    const int sharex_baseline = (std::min)(
        (std::max)(policy.automatic_minimum_pixels,
                   image_width / policy.automatic_width_divisor),
        image_width / policy.automatic_maximum_width_divisor);
    const int maximum_side =
        (std::max)(0, image_width - policy.minimum_center_pixels) / 2;
    range.left_pixels = (std::min)(sharex_baseline, maximum_side);
    range.right_pixels = range.left_pixels;
  } else {
    if (policy.left_pixels < 0 || policy.right_pixels < 0) return range;
    range.left_pixels = policy.left_pixels;
    range.right_pixels = policy.right_pixels;
  }

  const std::int64_t usable_width =
      static_cast<std::int64_t>(image_width) - range.left_pixels -
      range.right_pixels;
  if (usable_width <= 0 || usable_width > std::numeric_limits<int>::max()) {
    return range;
  }
  range.usable_width = static_cast<int>(usable_width);
  range.valid = true;
  return range;
}

const char* overlapRejectReasonName(OverlapRejectReason reason) noexcept {
  switch (reason) {
    case OverlapRejectReason::None:
      return "none";
    case OverlapRejectReason::InvalidImage:
      return "invalid_image";
    case OverlapRejectReason::WidthMismatch:
      return "width_mismatch";
    case OverlapRejectReason::NoCandidate:
      return "no_candidate";
    case OverlapRejectReason::InconsistentRegions:
      return "inconsistent_regions";
    case OverlapRejectReason::InsufficientTexture:
      return "insufficient_texture";
    case OverlapRejectReason::AmbiguousCandidates:
      return "ambiguous_candidates";
    case OverlapRejectReason::DisplacementOutOfRange:
      return "displacement_out_of_range";
    case OverlapRejectReason::InvalidEdgeExclusion:
      return "invalid_edge_exclusion";
  }
  return "unknown";
}

struct ImageStitcher::CandidateEvidence {
  int overlap_rows{0};
  int displacement_rows{0};
  std::uint16_t score_per_mille{0};
  std::uint16_t vertical_texture_per_mille{0};
  std::uint8_t consistent_column_bands{0};
  std::uint8_t sampled_column_bands{0};
  std::uint8_t consistent_row_bands{0};
  std::uint8_t sampled_row_bands{0};
  bool global_match{false};
  bool regions_consistent{false};
  bool texture_sufficient{false};
  bool displacement_valid{false};
};

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

bool ImageStitcher::validEdgeExclusions(const Image& accumulated,
                                        const Image& next,
                                        const SideExclusionRange& sides) const {
  if (!sides.valid || options_.fixed_top_rows < 0 ||
      options_.fixed_bottom_rows < 0) {
    return false;
  }
  const int minimum = std::max(1, options_.min_overlap_rows);
  const auto hasBody = [&](const Image& image) {
    return options_.fixed_top_rows <= image.height &&
           options_.fixed_bottom_rows <=
               image.height - options_.fixed_top_rows &&
           image.height - options_.fixed_top_rows -
                   options_.fixed_bottom_rows >=
               minimum;
  };
  return hasBody(accumulated) && hasBody(next);
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

ImageStitcher::CandidateEvidence ImageStitcher::evaluateCandidate(
    const Image& accumulated, const Image& next, int overlap_rows,
    const SideExclusionRange& sides) const {
  CandidateEvidence evidence;
  evidence.overlap_rows = overlap_rows;
  evidence.displacement_rows =
      next.height - options_.fixed_top_rows -
      options_.fixed_bottom_rows - overlap_rows;
  const int step = std::max(1, options_.sample_step);
  const int width = accumulated.width;
  const int first_x = sides.left_pixels;
  const int last_x = width - sides.right_pixels;
  if (first_x >= last_x) return evidence;

  const std::uint16_t required_match_per_mille =
      std::min<std::uint16_t>(options_.minimum_match_per_mille, 1000u);
  const std::uint16_t regional_match_per_mille =
      required_match_per_mille > 100
          ? static_cast<std::uint16_t>(required_match_per_mille - 100)
          : 0;
  std::uint64_t matched_samples = 0;
  std::uint64_t total_samples = 0;
  std::array<std::uint64_t, 3> column_matches{};
  std::array<std::uint64_t, 3> column_totals{};
  std::array<std::uint64_t, 3> row_matches{};
  std::array<std::uint64_t, 3> row_totals{};
  std::uint64_t previous_row_hash = 0;
  std::uint64_t texture_transitions = 0;
  std::uint64_t texture_opportunities = 0;
  bool has_previous_row_hash = false;

  const auto pixelAt = [](const Image& image, int x, int y) {
    return image.pixels[static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(image.width) +
                        static_cast<std::size_t>(x)];
  };
  const auto recordMatch = [&](int x, int accumulated_y, int next_y,
                               int column_band, int row_band) {
    ++total_samples;
    ++column_totals[static_cast<std::size_t>(column_band)];
    ++row_totals[static_cast<std::size_t>(row_band)];
    if (pixelsMatch(pixelAt(accumulated, x, accumulated_y),
                    pixelAt(next, x, next_y))) {
      ++matched_samples;
      ++column_matches[static_cast<std::size_t>(column_band)];
      ++row_matches[static_cast<std::size_t>(row_band)];
    }
  };
  const auto recordSampledRow = [&](int accumulated_y, int next_y) {
    const int relative_next_y = next_y - options_.fixed_top_rows;
    const int row_band = (std::min)(
        2, relative_next_y * 3 / (std::max)(1, overlap_rows));
    std::uint64_t row_hash = 1469598103934665603ull;
    const auto recordX = [&](int x) {
      const int column_band = (std::min)(
          2, (x - first_x) * 3 / (std::max)(1, last_x - first_x));
      recordMatch(x, accumulated_y, next_y, column_band, row_band);
      row_hash ^= pixelAt(accumulated, x, accumulated_y);
      row_hash *= 1099511628211ull;
    };
    for (int x = first_x; x < last_x; x += step) recordX(x);
    const int rightmost_x = last_x - 1;
    if ((rightmost_x - first_x) % step != 0) recordX(rightmost_x);
    if (has_previous_row_hash) {
      ++texture_opportunities;
      if (row_hash != previous_row_hash) ++texture_transitions;
    }
    previous_row_hash = row_hash;
    has_previous_row_hash = true;
  };

  for (int y = 0; y < overlap_rows; y += step) {
    recordSampledRow(accumulated.height - options_.fixed_bottom_rows -
                         overlap_rows + y,
                     options_.fixed_top_rows + y);
  }
  if ((overlap_rows - 1) % step != 0) {
    recordSampledRow(accumulated.height - options_.fixed_bottom_rows - 1,
                     options_.fixed_top_rows + overlap_rows - 1);
  }
  if (total_samples == 0) return evidence;

  evidence.score_per_mille = static_cast<std::uint16_t>(
      (std::min<std::uint64_t>)(1000u,
          matched_samples * 1000u / total_samples));
  evidence.global_match =
      matched_samples * 1000u >=
      total_samples * required_match_per_mille;

  const auto countConsistentBands = [regional_match_per_mille](
      const std::array<std::uint64_t, 3>& matches,
      const std::array<std::uint64_t, 3>& totals,
      std::uint8_t& sampled) {
    std::uint8_t consistent = 0;
    sampled = 0;
    for (std::size_t band = 0; band < totals.size(); ++band) {
      if (totals[band] == 0) continue;
      ++sampled;
      if (matches[band] * 1000u >=
          totals[band] * regional_match_per_mille) {
        ++consistent;
      }
    }
    return consistent;
  };
  evidence.consistent_column_bands = countConsistentBands(
      column_matches, column_totals, evidence.sampled_column_bands);
  evidence.consistent_row_bands = countConsistentBands(
      row_matches, row_totals, evidence.sampled_row_bands);
  evidence.regions_consistent =
      evidence.sampled_column_bands > 0 &&
      evidence.consistent_column_bands == evidence.sampled_column_bands &&
      evidence.sampled_row_bands > 0 &&
      evidence.consistent_row_bands == evidence.sampled_row_bands;

  if (texture_opportunities > 0) {
    evidence.vertical_texture_per_mille = static_cast<std::uint16_t>(
        texture_transitions * 1000u / texture_opportunities);
  }
  evidence.texture_sufficient =
      evidence.vertical_texture_per_mille >=
      (std::min<std::uint16_t>)(
          options_.minimum_vertical_texture_per_mille, 1000u);
  evidence.displacement_valid =
      evidence.displacement_rows >= options_.minimum_displacement_rows &&
      (options_.maximum_displacement_rows <= 0 ||
       evidence.displacement_rows <= options_.maximum_displacement_rows);
  return evidence;
}

bool ImageStitcher::findOverlap(const Image& accumulated, const Image& next,
                                int& overlap_rows) const {
  overlap_rows = 0;
  OverlapEvidence evidence;
  if (!findOverlap(accumulated, next, evidence)) return false;
  overlap_rows = evidence.overlap_rows;
  return true;
}

bool ImageStitcher::findOverlap(const Image& accumulated, const Image& next,
                                OverlapEvidence& evidence) const {
  evidence = OverlapEvidence{};
  if (!validImage(accumulated) || !validImage(next)) {
    evidence.reject_reason = OverlapRejectReason::InvalidImage;
    return false;
  }
  if (accumulated.width != next.width) {
    evidence.reject_reason = OverlapRejectReason::WidthMismatch;
    return false;
  }
  const SideExclusionRange sides =
      resolveSideExclusion(accumulated.width, options_.side_exclusion);
  evidence.excluded_left_pixels = sides.left_pixels;
  evidence.excluded_right_pixels = sides.right_pixels;
  evidence.usable_match_width = sides.usable_width;
  if (!validEdgeExclusions(accumulated, next, sides)) {
    evidence.reject_reason = OverlapRejectReason::InvalidEdgeExclusion;
    return true;
  }

  const int minimum = std::max(1, options_.min_overlap_rows);
  const int accumulated_body_rows =
      accumulated.height - options_.fixed_top_rows -
      options_.fixed_bottom_rows;
  const int next_body_rows = next.height - options_.fixed_top_rows -
                             options_.fixed_bottom_rows;
  int maximum = std::min(accumulated_body_rows, next_body_rows);
  if (options_.max_overlap_rows > 0) {
    maximum = std::min(maximum, options_.max_overlap_rows);
  }
  if (minimum > maximum) {
    evidence.reject_reason = OverlapRejectReason::NoCandidate;
    return true;
  }

  CandidateEvidence best_global;
  CandidateEvidence best;
  CandidateEvidence second_best;
  bool saw_region_failure = false;
  bool saw_texture_failure = false;
  bool saw_displacement_failure = false;
  for (int candidate = maximum; candidate >= minimum; --candidate) {
    const CandidateEvidence inspected =
        evaluateCandidate(accumulated, next, candidate, sides);
    if (!inspected.global_match) continue;
    if (inspected.score_per_mille > best_global.score_per_mille) {
      best_global = inspected;
    }
    if (!inspected.regions_consistent) {
      saw_region_failure = true;
      continue;
    }
    if (!inspected.texture_sufficient) {
      saw_texture_failure = true;
      continue;
    }
    if (!inspected.displacement_valid) {
      saw_displacement_failure = true;
      continue;
    }

    ++evidence.matching_candidates;
    if (inspected.score_per_mille > best.score_per_mille ||
        (inspected.score_per_mille == best.score_per_mille &&
         inspected.overlap_rows > best.overlap_rows)) {
      second_best = best;
      best = inspected;
    } else if (inspected.score_per_mille >
               second_best.score_per_mille) {
      second_best = inspected;
    }
  }

  const CandidateEvidence reported = best.overlap_rows > 0 ? best
                                                            : best_global;
  evidence.candidate_overlap_rows = reported.overlap_rows;
  evidence.displacement_rows = reported.displacement_rows;
  evidence.best_score_per_mille = reported.score_per_mille;
  evidence.second_best_score_per_mille = second_best.score_per_mille;
  evidence.vertical_texture_per_mille =
      reported.vertical_texture_per_mille;
  evidence.consistent_column_bands = reported.consistent_column_bands;
  evidence.sampled_column_bands = reported.sampled_column_bands;
  evidence.consistent_row_bands = reported.consistent_row_bands;
  evidence.sampled_row_bands = reported.sampled_row_bands;

  if (evidence.matching_candidates == 0) {
    evidence.reject_reason =
        saw_displacement_failure
            ? OverlapRejectReason::DisplacementOutOfRange
            : saw_texture_failure
                ? OverlapRejectReason::InsufficientTexture
                : saw_region_failure
                    ? OverlapRejectReason::InconsistentRegions
                    : OverlapRejectReason::NoCandidate;
    return true;
  }

  const int score_margin = static_cast<int>(best.score_per_mille) -
                           static_cast<int>(second_best.score_per_mille);
  if (options_.require_unique_overlap &&
      evidence.matching_candidates > 1 &&
      score_margin < options_.minimum_score_margin_per_mille) {
    evidence.reject_reason = OverlapRejectReason::AmbiguousCandidates;
    return true;
  }

  evidence.overlap_rows = best.overlap_rows;
  evidence.candidate_overlap_rows = best.overlap_rows;
  evidence.displacement_rows = best.displacement_rows;
  evidence.accumulated_keep_rows =
      accumulated.height - options_.fixed_bottom_rows;
  evidence.next_append_start_row =
      options_.fixed_top_rows + best.overlap_rows;
  evidence.next_append_rows =
      next.height - evidence.next_append_start_row;
  const std::int64_t output_rows =
      static_cast<std::int64_t>(evidence.accumulated_keep_rows) +
      static_cast<std::int64_t>(evidence.next_append_rows);
  if (output_rows > std::numeric_limits<int>::max()) {
    evidence.overlap_rows = 0;
    evidence.reject_reason = OverlapRejectReason::DisplacementOutOfRange;
    return true;
  }
  evidence.output_rows = static_cast<int>(output_rows);
  evidence.reject_reason = OverlapRejectReason::None;
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

  OverlapEvidence evidence;
  if (!findOverlap(accumulated, next, evidence)) {
    return false;
  }
  const bool fixed_edges_enabled =
      options_.fixed_top_rows > 0 || options_.fixed_bottom_rows > 0;
  if (!evidence.accepted() &&
      (evidence.reject_reason != OverlapRejectReason::NoCandidate ||
       options_.require_overlap || fixed_edges_enabled)) {
    return false;
  }
  if (!evidence.accepted()) {
    evidence.accumulated_keep_rows = accumulated.height;
    evidence.next_append_start_row = 0;
    evidence.next_append_rows = next.height;
    if (next.height > std::numeric_limits<int>::max() - accumulated.height) {
      return false;
    }
    evidence.output_rows = accumulated.height + next.height;
  }

  if (!appendDetected(accumulated, next, evidence)) return false;
  if (overlap_rows != nullptr) {
    *overlap_rows = evidence.overlap_rows;
  }
  return true;
}

bool ImageStitcher::appendDetected(Image& accumulated, const Image& next,
                                   const OverlapEvidence& evidence) const {

  const std::size_t width = static_cast<std::size_t>(next.width);
  if (evidence.accumulated_keep_rows < 0 ||
      evidence.accumulated_keep_rows > accumulated.height ||
      evidence.next_append_start_row < 0 ||
      evidence.next_append_start_row > next.height ||
      evidence.next_append_rows !=
          next.height - evidence.next_append_start_row ||
      evidence.output_rows != evidence.accumulated_keep_rows +
                                  evidence.next_append_rows ||
      evidence.output_rows <= 0) {
    return false;
  }
  const std::size_t accumulated_keep_rows =
      static_cast<std::size_t>(evidence.accumulated_keep_rows);
  const std::size_t next_append_rows =
      static_cast<std::size_t>(evidence.next_append_rows);
  const std::size_t max_pixels_per_width =
      std::numeric_limits<std::size_t>::max() / width;
  if (next_append_rows > max_pixels_per_width ||
      accumulated_keep_rows > max_pixels_per_width - next_append_rows ||
      next_append_rows >
          static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      accumulated_keep_rows >
          static_cast<std::size_t>(std::numeric_limits<int>::max()) -
              next_append_rows) {
    return false;
  }

  const std::size_t new_pixel_count = static_cast<std::size_t>(
      evidence.output_rows) * width;
  ImagePixels merged;
  try {
    merged.reserve(new_pixel_count);
    const std::size_t accumulated_keep_pixels = accumulated_keep_rows * width;
    merged.insert(merged.end(), accumulated.pixels.begin(),
                  accumulated.pixels.begin() + static_cast<std::ptrdiff_t>(
                      accumulated_keep_pixels));
    const std::size_t first_new_pixel =
        static_cast<std::size_t>(evidence.next_append_start_row) * width;
    merged.insert(merged.end(), next.pixels.begin() +
                                    static_cast<std::ptrdiff_t>(first_new_pixel),
                  next.pixels.end());
  } catch (const std::bad_alloc&) {
    throw;
  } catch (...) {
    return false;
  }

  accumulated.height = evidence.output_rows;
  accumulated.pixels.swap(merged);
  return true;
}

bool ImageStitcher::append(Image& accumulated, const Image& next,
                           OverlapEvidence& evidence) const {
  evidence = OverlapEvidence{};
  if (!validImage(next)) {
    evidence.reject_reason = OverlapRejectReason::InvalidImage;
    return false;
  }
  if (accumulated.empty()) {
    accumulated = next;
    evidence.reject_reason = OverlapRejectReason::None;
    return true;
  }
  if (!findOverlap(accumulated, next, evidence)) return false;
  const bool fixed_edges_enabled =
      options_.fixed_top_rows > 0 || options_.fixed_bottom_rows > 0;
  if (!evidence.accepted() &&
      (evidence.reject_reason != OverlapRejectReason::NoCandidate ||
       options_.require_overlap || fixed_edges_enabled)) {
    return false;
  }
  if (!evidence.accepted()) {
    evidence.accumulated_keep_rows = accumulated.height;
    evidence.next_append_start_row = 0;
    evidence.next_append_rows = next.height;
    if (next.height > std::numeric_limits<int>::max() - accumulated.height) {
      return false;
    }
    evidence.output_rows = accumulated.height + next.height;
  }
  return appendDetected(accumulated, next, evidence);
}

}  // namespace qingying
