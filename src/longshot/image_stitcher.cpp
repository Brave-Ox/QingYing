#include "qingying/longshot/image_stitcher.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

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
    const Image& accumulated, const Image& next, int overlap_rows) const {
  CandidateEvidence evidence;
  evidence.overlap_rows = overlap_rows;
  evidence.displacement_rows = next.height - overlap_rows;
  const int step = std::max(1, options_.sample_step);
  const int width = accumulated.width;
  const int first_x = std::max(0, options_.left_edge_exclusion_pixels);
  const int last_x =
      width - std::max(0, options_.right_edge_exclusion_pixels);
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
    const int row_band =
        (std::min)(2, next_y * 3 / (std::max)(1, overlap_rows));
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
    recordSampledRow(accumulated.height - overlap_rows + y, y);
  }
  if ((overlap_rows - 1) % step != 0) {
    recordSampledRow(accumulated.height - 1, overlap_rows - 1);
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

  const int minimum = std::max(1, options_.min_overlap_rows);
  int maximum = std::min(accumulated.height, next.height);
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
        evaluateCandidate(accumulated, next, candidate);
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
  if (!evidence.accepted() &&
      (evidence.reject_reason != OverlapRejectReason::NoCandidate ||
       options_.require_overlap)) {
    return false;
  }
  const int detected_overlap = evidence.overlap_rows;

  if (!appendDetected(accumulated, next, detected_overlap)) return false;
  if (overlap_rows != nullptr) {
    *overlap_rows = detected_overlap;
  }
  return true;
}

bool ImageStitcher::appendDetected(Image& accumulated, const Image& next,
                                   int detected_overlap) const {

  const std::size_t width = static_cast<std::size_t>(next.width);
  const std::size_t accumulated_height =
      static_cast<std::size_t>(accumulated.height);
  const std::size_t next_height = static_cast<std::size_t>(next.height);
  const std::size_t new_rows = next_height -
                               static_cast<std::size_t>(detected_overlap);
  const std::size_t max_pixels_per_width =
      std::numeric_limits<std::size_t>::max() / width;
  if (new_rows > max_pixels_per_width ||
      accumulated_height > max_pixels_per_width - new_rows ||
      new_rows > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      accumulated_height >
          static_cast<std::size_t>(std::numeric_limits<int>::max()) -
              new_rows) {
    return false;
  }

  const std::size_t new_pixel_count =
      (accumulated_height + new_rows) * width;
  ImagePixels merged;
  try {
    merged.reserve(new_pixel_count);
    merged.insert(merged.end(), accumulated.pixels.begin(),
                  accumulated.pixels.end());
    const std::size_t first_new_pixel =
        static_cast<std::size_t>(detected_overlap) * width;
    merged.insert(merged.end(), next.pixels.begin() +
                                    static_cast<std::ptrdiff_t>(first_new_pixel),
                  next.pixels.end());
  } catch (const std::bad_alloc&) {
    throw;
  } catch (...) {
    return false;
  }

  accumulated.height = static_cast<int>(accumulated_height + new_rows);
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
  if (!evidence.accepted() &&
      (evidence.reject_reason != OverlapRejectReason::NoCandidate ||
       options_.require_overlap)) {
    return false;
  }
  return appendDetected(accumulated, next, evidence.overlap_rows);
}

}  // namespace qingying
