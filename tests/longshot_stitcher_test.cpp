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

Image makeFixedEdgeFrame(int width, int top_rows, int body_rows,
                         int bottom_rows, int first_body_row,
                         std::uint32_t top_pixel,
                         std::uint32_t bottom_pixel) {
  Image image;
  image.width = width;
  image.height = top_rows + body_rows + bottom_rows;
  image.pixels.resize(static_cast<std::size_t>(width) * image.height);
  for (int y = 0; y < image.height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::uint32_t pixel =
          y < top_rows
              ? top_pixel
              : y >= top_rows + body_rows
                    ? bottom_pixel
                    : pixelFor(x, first_body_row + y - top_rows);
      image.pixels[static_cast<std::size_t>(y) * width + x] = pixel;
    }
  }
  return image;
}

void expectSolidRow(const Image& image, int row, std::uint32_t pixel) {
  for (int x = 0; x < image.width; ++x) {
    EXPECT_EQ(image.pixels[static_cast<std::size_t>(row) * image.width + x],
              pixel);
  }
}

void expectBodyRow(const Image& image, int row, int global_y) {
  for (int x = 0; x < image.width; ++x) {
    EXPECT_EQ(image.pixels[static_cast<std::size_t>(row) * image.width + x],
              pixelFor(x, global_y));
  }
}

void expectSameImage(const Image& actual, const Image& expected) {
  EXPECT_EQ(actual.width, expected.width);
  EXPECT_EQ(actual.height, expected.height);
  EXPECT_EQ(actual.pixels, expected.pixels);
}

}  // namespace

TEST(SideExclusionPolicyTest, UsesShareXBaselineAndPreservesNarrowCenter) {
  SideExclusionPolicy policy;
  policy.mode = SideExclusionMode::Automatic;

  const auto wide = resolveSideExclusion(1920, policy);
  EXPECT_TRUE(wide.valid);
  EXPECT_EQ(wide.left_pixels, 96);
  EXPECT_EQ(wide.right_pixels, 96);
  EXPECT_EQ(wide.usable_width, 1728);

  const auto medium = resolveSideExclusion(100, policy);
  EXPECT_TRUE(medium.valid);
  EXPECT_EQ(medium.left_pixels, 33);
  EXPECT_EQ(medium.right_pixels, 33);
  EXPECT_EQ(medium.usable_width, 34);

  const auto narrow = resolveSideExclusion(30, policy);
  EXPECT_TRUE(narrow.valid);
  EXPECT_EQ(narrow.left_pixels, 3);
  EXPECT_EQ(narrow.right_pixels, 3);
  EXPECT_EQ(narrow.usable_width, 24);

  const auto smaller_than_center = resolveSideExclusion(20, policy);
  EXPECT_TRUE(smaller_than_center.valid);
  EXPECT_EQ(smaller_than_center.left_pixels, 0);
  EXPECT_EQ(smaller_than_center.right_pixels, 0);
  EXPECT_EQ(smaller_than_center.usable_width, 20);
}

TEST(SideExclusionPolicyTest, RejectsInvalidExplicitRanges) {
  SideExclusionPolicy policy;
  policy.left_pixels = 10;
  policy.right_pixels = 10;
  EXPECT_FALSE(resolveSideExclusion(20, policy).valid);

  policy.left_pixels = -1;
  policy.right_pixels = 0;
  EXPECT_FALSE(resolveSideExclusion(20, policy).valid);
}

TEST(SideExclusionPolicyTest, RejectsInvalidAutomaticConfiguration) {
  SideExclusionPolicy policy;
  policy.mode = SideExclusionMode::Automatic;
  policy.automatic_width_divisor = 0;
  EXPECT_FALSE(resolveSideExclusion(100, policy).valid);
}

TEST(FixedBottomDetectionTest, FindsBoundedTexturedBottomInCentralRange) {
  Image previous = makeStrip(120, 60, 0);
  Image current = makeStrip(120, 60, 10);
  for (Image* image : {&previous, &current}) {
    for (int y = 54; y < 60; ++y) {
      for (int x = 0; x < 120; ++x) {
        image->pixels[static_cast<std::size_t>(y) * 120u + x] =
            (x % 7 == 0) ? 0xFF203040u : 0xFFE0D0C0u;
      }
    }
  }
  SideExclusionPolicy policy;
  policy.mode = SideExclusionMode::Automatic;

  const auto evidence = detectFixedBottomEdge(
      previous, current, resolveSideExclusion(previous.width, policy));

  EXPECT_TRUE(evidence.valid);
  EXPECT_TRUE(evidence.detected);
  EXPECT_EQ(evidence.candidate_rows, 6);
  EXPECT_EQ(evidence.match_per_mille, 1000);
  EXPECT_GE(evidence.texture_per_mille, 20);
}

TEST(FixedBottomDetectionTest, RejectsFlatBlankAndUnboundedStaticAreas) {
  SideExclusionPolicy policy;
  policy.mode = SideExclusionMode::Automatic;
  const auto sides = resolveSideExclusion(120, policy);

  Image previous = makeStrip(120, 60, 0);
  Image current = makeStrip(120, 60, 10);
  for (Image* image : {&previous, &current}) {
    for (int y = 54; y < 60; ++y) {
      for (int x = 0; x < 120; ++x) {
        image->pixels[static_cast<std::size_t>(y) * 120u + x] =
            0xFFF0F0F0u;
      }
    }
  }
  const auto blank = detectFixedBottomEdge(previous, current, sides);
  EXPECT_TRUE(blank.valid);
  EXPECT_FALSE(blank.detected);
  EXPECT_EQ(blank.candidate_rows, 6);
  EXPECT_EQ(blank.texture_per_mille, 0);

  const Image unchanged = makeStrip(120, 60, 0);
  const auto unbounded =
      detectFixedBottomEdge(unchanged, unchanged, sides);
  EXPECT_TRUE(unbounded.valid);
  EXPECT_FALSE(unbounded.detected);
  EXPECT_GT(unbounded.candidate_rows, 60 / 3);
}

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
  options.side_exclusion.right_pixels = 2;
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

TEST(ImageStitcherFixedEdgeTest,
     KeepsFirstTopContinuousBodyAndLatestBottomAcrossFrames) {
  constexpr int kWidth = 24;
  constexpr int kTop = 2;
  constexpr int kBody = 12;
  constexpr int kBottom = 2;
  constexpr std::uint32_t kFirstTop = 0xFF101010u;
  constexpr std::uint32_t kSecondTop = 0xFF202020u;
  constexpr std::uint32_t kThirdTop = 0xFF303030u;
  constexpr std::uint32_t kFirstBottom = 0xFF404040u;
  constexpr std::uint32_t kSecondBottom = 0xFF505050u;
  constexpr std::uint32_t kThirdBottom = 0xFF606060u;
  Image accumulated = makeFixedEdgeFrame(
      kWidth, kTop, kBody, kBottom, 0, kFirstTop, kFirstBottom);
  const Image second = makeFixedEdgeFrame(
      kWidth, kTop, kBody, kBottom, 5, kSecondTop, kSecondBottom);
  const Image third = makeFixedEdgeFrame(
      kWidth, kTop, kBody, kBottom, 10, kThirdTop, kThirdBottom);
  ImageStitchOptions options;
  options.fixed_top_rows = kTop;
  options.fixed_bottom_rows = kBottom;
  options.require_overlap = true;
  ImageStitcher stitcher(options);

  OverlapEvidence second_evidence;
  ASSERT_TRUE(stitcher.append(accumulated, second, second_evidence));
  EXPECT_EQ(second_evidence.overlap_rows, 7);
  EXPECT_EQ(second_evidence.displacement_rows, 5);
  EXPECT_EQ(second_evidence.accumulated_keep_rows, 14);
  EXPECT_EQ(second_evidence.next_append_start_row, 9);
  EXPECT_EQ(second_evidence.next_append_rows, 7);
  EXPECT_EQ(second_evidence.output_rows, 21);
  EXPECT_EQ(second_evidence.consistent_row_bands, 3);
  EXPECT_EQ(second_evidence.sampled_row_bands, 3);

  OverlapEvidence third_evidence;
  ASSERT_TRUE(stitcher.append(accumulated, third, third_evidence));
  EXPECT_EQ(third_evidence.output_rows, 26);
  ASSERT_EQ(accumulated.height, 26);
  for (int row = 0; row < kTop; ++row) {
    expectSolidRow(accumulated, row, kFirstTop);
  }
  for (int global_y = 0; global_y <= 21; ++global_y) {
    expectBodyRow(accumulated, kTop + global_y, global_y);
  }
  for (int row = accumulated.height - kBottom;
       row < accumulated.height; ++row) {
    expectSolidRow(accumulated, row, kThirdBottom);
  }
}

TEST(ImageStitcherFixedEdgeTest,
     SideExclusionsAffectMatchingButPreserveSelectedWidth) {
  Image accumulated = makeStrip(30, 24, 0);
  Image next = makeStrip(30, 20, 10);
  for (int y = 0; y < next.height; ++y) {
    next.pixels[static_cast<std::size_t>(y) * next.width] = 0xFFABCDEFu;
    next.pixels[static_cast<std::size_t>(y) * next.width + 29] =
        0xFF123456u;
  }
  ImageStitchOptions options;
  options.side_exclusion.left_pixels = 1;
  options.side_exclusion.right_pixels = 1;
  options.require_overlap = true;
  ImageStitcher stitcher(options);

  ASSERT_TRUE(stitcher.append(accumulated, next));
  ASSERT_EQ(accumulated.width, 30);
  ASSERT_EQ(accumulated.height, 30);
  EXPECT_EQ(accumulated.pixels[24u * 30u], 0xFFABCDEFu);
  EXPECT_EQ(accumulated.pixels[24u * 30u + 29u], 0xFF123456u);
}

TEST(ImageStitcherFixedEdgeTest,
     AutomaticSideExclusionsIgnoreBothEdgesAndReportEffectiveRange) {
  constexpr int kWidth = 120;
  constexpr int kHeight = 40;
  constexpr int kDisplacement = 10;
  constexpr int kExcludedSide = 40;
  Image accumulated = makeStrip(kWidth, kHeight, 0);
  Image next = makeStrip(kWidth, kHeight, kDisplacement);
  for (int y = 0; y < next.height; ++y) {
    for (int x = 0; x < next.width; ++x) {
      if (x < kExcludedSide || x >= next.width - kExcludedSide) {
        next.pixels[static_cast<std::size_t>(y) * next.width + x] =
            x < kExcludedSide ? 0xFFABCDEFu : 0xFF123456u;
      }
    }
  }
  ImageStitchOptions options;
  options.side_exclusion.mode = SideExclusionMode::Automatic;
  options.require_overlap = true;
  ImageStitcher stitcher(options);
  OverlapEvidence evidence;

  ASSERT_TRUE(stitcher.append(accumulated, next, evidence));
  EXPECT_EQ(evidence.overlap_rows, kHeight - kDisplacement);
  EXPECT_EQ(evidence.excluded_left_pixels, kExcludedSide);
  EXPECT_EQ(evidence.excluded_right_pixels, kExcludedSide);
  EXPECT_EQ(evidence.usable_match_width, 40);
  ASSERT_EQ(accumulated.width, kWidth);
  ASSERT_EQ(accumulated.height, kHeight + kDisplacement);
  EXPECT_EQ(accumulated.pixels[static_cast<std::size_t>(kHeight) * kWidth],
            0xFFABCDEFu);
  EXPECT_EQ(accumulated.pixels[
                static_cast<std::size_t>(kHeight) * kWidth + kWidth - 1],
            0xFF123456u);
}

TEST(ImageStitcherFixedEdgeTest,
     AutomaticSideExclusionsKeepLegacySeamWithoutEdgeInterference) {
  const Image first = makeStrip(120, 40, 0);
  const Image next = makeStrip(120, 40, 10);
  Image explicit_output = first;
  Image automatic_output = first;
  ImageStitchOptions explicit_options;
  explicit_options.require_overlap = true;
  ImageStitchOptions automatic_options = explicit_options;
  automatic_options.side_exclusion.mode = SideExclusionMode::Automatic;
  ImageStitcher explicit_stitcher(explicit_options);
  ImageStitcher automatic_stitcher(automatic_options);
  OverlapEvidence explicit_evidence;
  OverlapEvidence automatic_evidence;

  ASSERT_TRUE(explicit_stitcher.append(explicit_output, next,
                                       explicit_evidence));
  ASSERT_TRUE(automatic_stitcher.append(automatic_output, next,
                                        automatic_evidence));
  EXPECT_EQ(automatic_evidence.overlap_rows, explicit_evidence.overlap_rows);
  EXPECT_EQ(automatic_evidence.output_rows, explicit_evidence.output_rows);
  expectSameImage(automatic_output, explicit_output);
}

TEST(ImageStitcherFixedEdgeTest,
     InvalidOrUnverifiedEdgesAreRejectedWithoutMutation) {
  for (int mode = 0; mode < 2; ++mode) {
    SCOPED_TRACE(mode);
    Image accumulated = makeFixedEdgeFrame(
        20, 2, 12, 2, 0, 0xFF101010u, 0xFF202020u);
    const Image original = accumulated;
    const Image next = mode == 0
                           ? makeFixedEdgeFrame(
                                 20, 2, 12, 2, 100, 0xFF101010u,
                                 0xFF303030u)
                           : makeFixedEdgeFrame(
                                 20, 2, 12, 2, 4, 0xFF101010u,
                                 0xFF303030u);
    ImageStitchOptions options;
    options.fixed_top_rows = mode == 0 ? 2 : 9;
    options.fixed_bottom_rows = mode == 0 ? 2 : 7;
    options.require_overlap = false;
    ImageStitcher stitcher(options);
    OverlapEvidence evidence;

    EXPECT_FALSE(stitcher.append(accumulated, next, evidence));
    EXPECT_EQ(evidence.reject_reason,
              mode == 0 ? OverlapRejectReason::NoCandidate
                        : OverlapRejectReason::InvalidEdgeExclusion);
    expectSameImage(accumulated, original);
  }
}

}  // namespace qingying
