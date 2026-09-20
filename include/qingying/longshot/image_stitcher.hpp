#pragma once

#include "qingying/action/image.hpp"

#include <cstdint>

namespace qingying {

// Options for matching the bottom of an accumulated image with the top of
// the next viewport frame.
struct ImageStitchOptions {
  int min_overlap_rows{1};
  int max_overlap_rows{0};  // 0 means no explicit upper bound.
  int sample_step{4};
  std::uint8_t channel_tolerance{0};
  int left_edge_exclusion_pixels{0};
  int right_edge_exclusion_pixels{0};
  std::uint16_t minimum_match_per_mille{1000};
  std::uint16_t minimum_score_margin_per_mille{20};
  std::uint16_t minimum_vertical_texture_per_mille{100};
  int minimum_displacement_rows{0};
  int maximum_displacement_rows{0};  // 0 means no explicit upper bound.
  bool require_unique_overlap{true};
  bool require_overlap{false};
};

enum class OverlapRejectReason : std::uint8_t {
  None,
  InvalidImage,
  WidthMismatch,
  NoCandidate,
  InconsistentRegions,
  InsufficientTexture,
  AmbiguousCandidates,
  DisplacementOutOfRange,
};

const char* overlapRejectReasonName(OverlapRejectReason reason) noexcept;

struct OverlapEvidence {
  int overlap_rows{0};
  // Best visual candidate even when a safety rule rejects it. Callers must
  // still use accepted() before treating this value as stitchable overlap.
  int candidate_overlap_rows{0};
  int displacement_rows{0};
  int matching_candidates{0};
  std::uint16_t best_score_per_mille{0};
  std::uint16_t second_best_score_per_mille{0};
  std::uint16_t vertical_texture_per_mille{0};
  std::uint8_t consistent_column_bands{0};
  std::uint8_t sampled_column_bands{0};
  std::uint8_t consistent_row_bands{0};
  std::uint8_t sampled_row_bands{0};
  OverlapRejectReason reject_reason{OverlapRejectReason::NoCandidate};

  bool accepted() const noexcept {
    return reject_reason == OverlapRejectReason::None && overlap_rows > 0;
  }
};

class ImageStitcher {
 public:
  explicit ImageStitcher(ImageStitchOptions options = {});

  // Returns true when the two images are compatible and the overlap search
  // completed. overlap_rows may be zero: adjacent frames do not have to
  // overlap for append() to succeed.
  bool findOverlap(const Image& accumulated, const Image& next,
                  int& overlap_rows) const;
  // Returns false only when the image inputs themselves are invalid. A valid
  // inspection can still reject every candidate; evidence then carries the
  // stable reason and overlap_rows remains zero.
  bool findOverlap(const Image& accumulated, const Image& next,
                   OverlapEvidence& evidence) const;

  // Appends next below accumulated after removing the detected overlap. If
  // accumulated is empty, next becomes the first frame. When provided,
  // overlap_rows receives the number of removed rows.
  bool append(Image& accumulated, const Image& next,
              int* overlap_rows = nullptr) const;
  // Evidence is populated even when a valid-looking candidate is rejected.
  // Rejected candidates never mutate accumulated; a true no-overlap result
  // may still append adjacently when require_overlap is disabled.
  bool append(Image& accumulated, const Image& next,
              OverlapEvidence& evidence) const;

 private:
  struct CandidateEvidence;

  bool validImage(const Image& image) const;
  CandidateEvidence evaluateCandidate(const Image& accumulated,
                                      const Image& next,
                                      int overlap_rows) const;
  bool appendDetected(Image& accumulated, const Image& next,
                      int overlap_rows) const;
  bool pixelsMatch(std::uint32_t lhs, std::uint32_t rhs) const;

  ImageStitchOptions options_;
};

}  // namespace qingying
