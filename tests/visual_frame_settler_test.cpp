#include "visual_frame_settler.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace qingying {
namespace {

std::uint32_t stablePixelFor(int x, int global_y) {
  const auto b = static_cast<std::uint32_t>((global_y * 17 + x) & 0xFF);
  const auto g = static_cast<std::uint32_t>((global_y * 31 + x * 3) & 0xFF);
  const auto r = static_cast<std::uint32_t>((global_y * 47 + x * 5) & 0xFF);
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

Image stableStrip(int first_global_y) {
  constexpr int kWidth = 32;
  constexpr int kHeight = 40;
  Image image;
  image.width = kWidth;
  image.height = kHeight;
  image.pixels.resize(kWidth * kHeight);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      image.pixels[y * kWidth + x] = stablePixelFor(x, first_global_y + y);
    }
  }
  return image;
}

Image fixedEdgeStrip(int first_global_y, std::uint32_t edge_pixel) {
  constexpr int kWidth = 32;
  constexpr int kHeight = 40;
  constexpr int kTop = 2;
  constexpr int kBottom = 2;
  Image image;
  image.width = kWidth;
  image.height = kHeight;
  image.pixels.resize(kWidth * kHeight);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      image.pixels[y * kWidth + x] =
          y < kTop || y >= kHeight - kBottom
              ? edge_pixel
              : stablePixelFor(x, first_global_y + y - kTop);
    }
  }
  return image;
}

using longshot_detail::VisualFrameDecision;
using longshot_detail::VisualFrameSettler;

}  // namespace

TEST(VisualFrameSettlerTest, DelayedRepaintWaitsForRepeatedMovement) {
  const Image accumulated = stableStrip(0);
  const Image moved = stableStrip(10);
  VisualFrameSettler settler;

  EXPECT_EQ(settler.observe(accumulated, accumulated).decision,
            VisualFrameDecision::ObserveMore);
  EXPECT_EQ(settler.observe(accumulated, moved).decision,
            VisualFrameDecision::ObserveMore);
  const auto stable = settler.observe(accumulated, moved);
  EXPECT_EQ(stable.decision, VisualFrameDecision::StableMovement);
  EXPECT_EQ(stable.overlap_rows, 30);
}

TEST(VisualFrameSettlerTest, SmoothScrollAcceptsOnlyTheSettledPosition) {
  const Image accumulated = stableStrip(0);
  VisualFrameSettler settler;

  for (const int offset : {5, 10, 15}) {
    EXPECT_EQ(settler.observe(accumulated, stableStrip(offset)).decision,
              VisualFrameDecision::ObserveMore);
  }
  const auto stable = settler.observe(accumulated, stableStrip(15));
  EXPECT_EQ(stable.decision, VisualFrameDecision::StableMovement);
  EXPECT_EQ(stable.overlap_rows, 25);
}

TEST(VisualFrameSettlerTest, SparseCursorBlinkDoesNotImplyMovement) {
  const Image accumulated = stableStrip(0);
  Image blinking = accumulated;
  blinking.pixels[12 * blinking.width + 8] ^= 0x00010101u;
  VisualFrameSettler settler;

  EXPECT_EQ(settler.observe(accumulated, blinking).decision,
            VisualFrameDecision::ObserveMore);
  EXPECT_EQ(settler.observe(accumulated, accumulated).decision,
            VisualFrameDecision::ObserveMore);
  EXPECT_EQ(settler.observe(accumulated, blinking).decision,
            VisualFrameDecision::NoProgress);
}

TEST(VisualFrameSettlerTest, StaticFramesNeedThreeSamplesForNoProgress) {
  const Image accumulated = stableStrip(0);
  VisualFrameSettler settler;

  EXPECT_EQ(settler.observe(accumulated, accumulated).decision,
            VisualFrameDecision::ObserveMore);
  EXPECT_EQ(settler.observe(accumulated, accumulated).decision,
            VisualFrameDecision::ObserveMore);
  EXPECT_EQ(settler.observe(accumulated, accumulated).decision,
            VisualFrameDecision::NoProgress);
}

TEST(VisualFrameSettlerTest,
     FlatStaticFramesNeedThreeSamplesAndAreNeverAcceptedAsMovement) {
  Image flat;
  flat.width = 32;
  flat.height = 40;
  flat.pixels.assign(flat.width * flat.height, 0xFFF2F2F2u);
  VisualFrameSettler settler;

  for (int sample = 0; sample < 2; ++sample) {
    const auto observation = settler.observe(flat, flat);
    EXPECT_EQ(observation.decision, VisualFrameDecision::ObserveMore);
    EXPECT_EQ(observation.reject_reason,
              OverlapRejectReason::InsufficientTexture);
  }
  const auto stopped = settler.observe(flat, flat);
  EXPECT_EQ(stopped.decision, VisualFrameDecision::NoProgress);
  EXPECT_EQ(stopped.overlap_rows, flat.height);
  EXPECT_EQ(stopped.reject_reason, OverlapRejectReason::InsufficientTexture);
}

TEST(VisualFrameSettlerTest,
     FixedEdgeChangesDoNotHideAStaticBodyOrImplyMovement) {
  const Image accumulated = fixedEdgeStrip(0, 0xFF101010u);
  const Image changed_edges = fixedEdgeStrip(0, 0xFF202020u);
  ImageStitchOptions options;
  options.min_overlap_rows = 16;
  options.fixed_top_rows = 2;
  options.fixed_bottom_rows = 2;
  options.require_overlap = true;
  VisualFrameSettler settler(options);

  for (int sample = 0; sample < 2; ++sample) {
    const auto observation = settler.observe(accumulated, changed_edges);
    EXPECT_EQ(observation.decision, VisualFrameDecision::ObserveMore);
    EXPECT_EQ(observation.displacement_rows, 0);
    EXPECT_EQ(observation.output_rows, accumulated.height);
  }
  EXPECT_EQ(settler.observe(accumulated, changed_edges).decision,
            VisualFrameDecision::NoProgress);
}

TEST(VisualFrameSettlerTest, LostOverlapStopsImmediately) {
  const Image accumulated = stableStrip(0);
  Image unrelated = accumulated;
  for (auto& pixel : unrelated.pixels) pixel = 0xFF7F11E3u;
  VisualFrameSettler settler;

  const auto observation = settler.observe(accumulated, unrelated);
  EXPECT_EQ(observation.decision, VisualFrameDecision::LostOverlap);
  EXPECT_EQ(observation.reject_reason, OverlapRejectReason::NoCandidate);
  EXPECT_EQ(settler.sampleCount(), 1);
}

}  // namespace qingying
