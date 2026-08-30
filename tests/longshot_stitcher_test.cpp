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

}  // namespace qingying
