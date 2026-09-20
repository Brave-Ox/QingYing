#include "qingying/longshot/image_stitcher.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace qingying {
namespace {

std::uint32_t pixelFor(int x, int global_y) {
  // Keep every generated row distinct so an overlap cannot be inferred from
  // a repeated flat-color region by accident.
  const std::uint32_t b = static_cast<std::uint32_t>((global_y * 17 + x) &
                                                      0xFF);
  const std::uint32_t g = static_cast<std::uint32_t>((global_y * 31 + x * 3) &
                                                      0xFF);
  const std::uint32_t r = static_cast<std::uint32_t>((global_y * 47 + x * 5) &
                                                      0xFF);
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

Image makeStrip(int width, int height, int first_global_y) {
  Image image;
  image.width = width;
  image.height = height;
  image.pixels.resize(static_cast<std::size_t>(width) *
                      static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] =
          pixelFor(x, first_global_y + y);
    }
  }
  return image;
}

Image makePeriodicStrip(int width, int height, int first_global_y,
                        int period) {
  Image image = makeStrip(width, height, first_global_y);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.pixels[static_cast<std::size_t>(y) * width + x] =
          pixelFor(x, (first_global_y + y) % period);
    }
  }
  return image;
}

void expectSameImage(const Image& actual, const Image& expected) {
  EXPECT_EQ(actual.width, expected.width);
  EXPECT_EQ(actual.height, expected.height);
  EXPECT_EQ(actual.pixels, expected.pixels);
}

}  // namespace

TEST(ImageStitcherTest, FindsLargestOverlapAndAppendsOnlyNewRows) {
  Image accumulated = makeStrip(8, 10, 0);  // rows 0..9
  const Image next = makeStrip(8, 9, 7);    // rows 7..15; overlap is 3
  ImageStitcher stitcher;

  int overlap = -1;
  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, overlap));
  EXPECT_EQ(overlap, 3);

  ASSERT_TRUE(stitcher.append(accumulated, next, &overlap));
  EXPECT_EQ(overlap, 3);
  EXPECT_EQ(accumulated.width, 8);
  EXPECT_EQ(accumulated.height, 16);
  EXPECT_EQ(accumulated.pixels.back(), pixelFor(7, 15));
}

TEST(ImageStitcherTest, AppendsAdjacentFramesWithoutOverlap) {
  Image accumulated = makeStrip(4, 5, 0);
  const Image next = makeStrip(4, 3, 20);
  ImageStitcher stitcher;

  int overlap = -1;
  ASSERT_TRUE(stitcher.append(accumulated, next, &overlap));
  EXPECT_EQ(overlap, 0);
  EXPECT_EQ(accumulated.height, 8);
  EXPECT_EQ(accumulated.pixels.back(), pixelFor(3, 22));
}

TEST(ImageStitcherTest, IdenticalFrameHasFullOverlap) {
  Image accumulated = makeStrip(5, 6, 100);
  const Image next = accumulated;
  ImageStitcher stitcher;

  int overlap = 0;
  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, overlap));
  EXPECT_EQ(overlap, 6);

  ASSERT_TRUE(stitcher.append(accumulated, next, &overlap));
  EXPECT_EQ(overlap, 6);
  EXPECT_EQ(accumulated.height, 6);
}

TEST(ImageStitcherTest, SupportsSmallChannelDifferences) {
  Image accumulated = makeStrip(6, 8, 0);
  Image next = makeStrip(6, 6, 5);  // overlap is 3
  next.pixels[0] += 1u;

  ImageStitchOptions options;
  options.channel_tolerance = 1;
  ImageStitcher stitcher(options);

  int overlap = 0;
  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, overlap));
  EXPECT_EQ(overlap, 3);
}

TEST(ImageStitcherTest, IgnoresMovingRightEdgeWhenFindingOverlap) {
  Image accumulated = makeStrip(40, 30, 0);  // rows 0..29
  Image next = makeStrip(40, 24, 6);         // rows 6..29; overlap is 24
  for (int y = 0; y < next.height; ++y) {
    for (int x = 38; x < next.width; ++x) {
      next.pixels[static_cast<std::size_t>(y) *
                      static_cast<std::size_t>(next.width) +
                  static_cast<std::size_t>(x)] = 0xFF101010u;
    }
  }

  ImageStitchOptions options;
  options.right_edge_exclusion_pixels = 2;
  ImageStitcher stitcher(options);

  int overlap = 0;
  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, overlap));
  EXPECT_EQ(overlap, 24);
}

TEST(ImageStitcherTest, ToleratesSparseDynamicPixelsWhenFindingOverlap) {
  Image accumulated = makeStrip(40, 30, 0);  // rows 0..29
  Image next = makeStrip(40, 24, 6);         // rows 6..29; overlap is 24
  for (int y = 0; y < next.height; y += 8) {
    next.pixels[static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(next.width) +
                12u] = 0xFF102030u;
  }

  ImageStitchOptions options;
  options.minimum_match_per_mille = 950;
  ImageStitcher stitcher(options);

  int overlap = 0;
  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, overlap));
  EXPECT_EQ(overlap, 24);
}

TEST(ImageStitcherTest, RefusesToAppendNonOverlappingFramesWhenRequired) {
  Image accumulated = makeStrip(8, 10, 0);
  const Image next = makeStrip(8, 6, 100);

  ImageStitchOptions options;
  options.require_overlap = true;
  ImageStitcher stitcher(options);

  int overlap = -1;
  EXPECT_FALSE(stitcher.append(accumulated, next, &overlap));
  EXPECT_EQ(overlap, 0);
  EXPECT_EQ(accumulated.height, 10);
}

TEST(ImageStitcherTest, RejectsDifferentWidths) {
  Image accumulated = makeStrip(4, 5, 0);
  const Image next = makeStrip(5, 3, 3);
  ImageStitcher stitcher;

  int overlap = -1;
  EXPECT_FALSE(stitcher.findOverlap(accumulated, next, overlap));
  EXPECT_FALSE(stitcher.append(accumulated, next, &overlap));
  EXPECT_EQ(accumulated.height, 5);
}

TEST(ImageStitcherTest, EmptyAccumulatedImageBecomesFirstFrame) {
  Image accumulated;
  const Image next = makeStrip(3, 4, 20);
  ImageStitcher stitcher;

  int overlap = -1;
  ASSERT_TRUE(stitcher.append(accumulated, next, &overlap));
  EXPECT_EQ(overlap, 0);
  EXPECT_EQ(accumulated.width, 3);
  EXPECT_EQ(accumulated.height, 4);
  EXPECT_EQ(accumulated.pixels, next.pixels);
}

TEST(ImageStitcherEvidenceTest, ReportsUniqueMultiBandOverlapEvidence) {
  const Image accumulated = makeStrip(36, 40, 0);
  const Image next = makeStrip(36, 32, 12);
  ImageStitchOptions options;
  options.sample_step = 2;
  options.require_overlap = true;
  ImageStitcher stitcher(options);
  OverlapEvidence evidence;

  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, evidence));

  EXPECT_TRUE(evidence.accepted());
  EXPECT_EQ(evidence.reject_reason, OverlapRejectReason::None);
  EXPECT_EQ(evidence.overlap_rows, 28);
  EXPECT_EQ(evidence.displacement_rows, 4);
  EXPECT_EQ(evidence.matching_candidates, 1);
  EXPECT_EQ(evidence.best_score_per_mille, 1000);
  EXPECT_EQ(evidence.consistent_column_bands, 3);
  EXPECT_EQ(evidence.sampled_column_bands, 3);
  EXPECT_EQ(evidence.consistent_row_bands, 3);
  EXPECT_EQ(evidence.sampled_row_bands, 3);
  EXPECT_GE(evidence.vertical_texture_per_mille, 100);
}

TEST(ImageStitcherEvidenceTest, RejectsFlatColorAndPreservesAccumulatedImage) {
  Image accumulated{24, 24,
                    std::vector<std::uint32_t>(24u * 24u, 0xFF334455u)};
  const Image original = accumulated;
  const Image next{24, 18,
                   std::vector<std::uint32_t>(24u * 18u, 0xFF334455u)};
  ImageStitchOptions options;
  options.require_overlap = true;
  ImageStitcher stitcher(options);
  OverlapEvidence evidence;

  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, evidence));
  EXPECT_FALSE(evidence.accepted());
  EXPECT_EQ(evidence.reject_reason,
            OverlapRejectReason::InsufficientTexture);
  EXPECT_STREQ(overlapRejectReasonName(evidence.reject_reason),
               "insufficient_texture");
  OverlapEvidence append_evidence;
  EXPECT_FALSE(stitcher.append(accumulated, next, append_evidence));
  EXPECT_EQ(append_evidence.reject_reason,
            OverlapRejectReason::InsufficientTexture);
  expectSameImage(accumulated, original);
}

TEST(ImageStitcherEvidenceTest,
     RejectsAmbiguousShortPeriodPatternAndPreservesAccumulatedImage) {
  Image accumulated = makePeriodicStrip(24, 24, 0, 4);
  const Image original = accumulated;
  const Image next = makePeriodicStrip(24, 16, 8, 4);
  ImageStitchOptions options;
  options.min_overlap_rows = 4;
  options.sample_step = 1;
  options.require_overlap = true;
  ImageStitcher stitcher(options);
  OverlapEvidence evidence;

  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, evidence));
  EXPECT_FALSE(evidence.accepted());
  EXPECT_EQ(evidence.reject_reason,
            OverlapRejectReason::AmbiguousCandidates);
  EXPECT_GT(evidence.matching_candidates, 1);
  EXPECT_EQ(evidence.best_score_per_mille, 1000);
  EXPECT_EQ(evidence.second_best_score_per_mille, 1000);
  OverlapEvidence append_evidence;
  EXPECT_FALSE(stitcher.append(accumulated, next, append_evidence));
  EXPECT_EQ(append_evidence.reject_reason,
            OverlapRejectReason::AmbiguousCandidates);
  expectSameImage(accumulated, original);
}

TEST(ImageStitcherEvidenceTest, RejectsMatchConfinedToOnlySomeColumnBands) {
  Image accumulated = makeStrip(30, 24, 0);
  const Image original = accumulated;
  Image next = makeStrip(30, 18, 6);
  for (int y = 0; y < next.height; ++y) {
    for (int x = 10; x < 20; ++x) {
      next.pixels[static_cast<std::size_t>(y) * next.width + x] =
          0xFF010203u;
    }
  }
  ImageStitchOptions options;
  options.sample_step = 1;
  options.minimum_match_per_mille = 600;
  options.require_overlap = true;
  ImageStitcher stitcher(options);
  OverlapEvidence evidence;

  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, evidence));
  EXPECT_FALSE(evidence.accepted());
  EXPECT_EQ(evidence.reject_reason,
            OverlapRejectReason::InconsistentRegions);
  EXPECT_LT(evidence.consistent_column_bands,
            evidence.sampled_column_bands);
  EXPECT_FALSE(stitcher.append(accumulated, next));
  expectSameImage(accumulated, original);
}

TEST(ImageStitcherEvidenceTest, RejectsDisplacementOutsideConfiguredRange) {
  Image accumulated = makeStrip(32, 40, 0);
  const Image original = accumulated;
  const Image next = makeStrip(32, 40, 10);
  ImageStitchOptions options;
  options.maximum_displacement_rows = 5;
  options.require_overlap = true;
  ImageStitcher stitcher(options);
  OverlapEvidence evidence;

  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, evidence));
  EXPECT_EQ(evidence.reject_reason,
            OverlapRejectReason::DisplacementOutOfRange);
  EXPECT_FALSE(stitcher.append(accumulated, next));
  expectSameImage(accumulated, original);
}

TEST(ImageStitcherEvidenceTest, ReportsTrueNoOverlapWithoutChangingPixels) {
  Image accumulated = makeStrip(24, 24, 0);
  const Image original = accumulated;
  const Image next = makeStrip(24, 18, 100);
  ImageStitchOptions options;
  options.require_overlap = true;
  ImageStitcher stitcher(options);
  OverlapEvidence evidence;

  ASSERT_TRUE(stitcher.findOverlap(accumulated, next, evidence));
  EXPECT_EQ(evidence.reject_reason, OverlapRejectReason::NoCandidate);
  EXPECT_EQ(evidence.overlap_rows, 0);
  EXPECT_FALSE(stitcher.append(accumulated, next));
  expectSameImage(accumulated, original);
}

}  // namespace qingying
