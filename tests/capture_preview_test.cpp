#include "qingying/app/capture_preview.hpp"

#include <gtest/gtest.h>

namespace qingying {

TEST(CapturePreviewTest, PastesSelectionUsingVirtualScreenCoordinates) {
  Image background{4, 3, std::vector<std::uint32_t>(12, 1u)};
  Image selection_image{2, 2, {10u, 11u, 12u, 13u}};
  ScreenPhysicalRect selection;
  selection.x = -9;
  selection.y = 21;
  selection.width = 2;
  selection.height = 2;
  const coord::VirtualScreenRect screen{-10, 20, 4, 3};

  ASSERT_TRUE(composeCapturePreview(background, selection_image, selection,
                                    screen));
  EXPECT_EQ(background.pixels[5], 10u);
  EXPECT_EQ(background.pixels[6], 11u);
  EXPECT_EQ(background.pixels[9], 12u);
  EXPECT_EQ(background.pixels[10], 13u);
  EXPECT_EQ(background.pixels[0], 1u);
}

TEST(CapturePreviewTest, ClipsSelectionAtDesktopEdges) {
  Image background{3, 2, std::vector<std::uint32_t>(6, 1u)};
  Image selection_image{2, 2, {10u, 11u, 12u, 13u}};
  ScreenPhysicalRect selection;
  selection.x = -1;
  selection.y = -1;
  selection.width = 2;
  selection.height = 2;
  const coord::VirtualScreenRect screen{0, 0, 3, 2};

  ASSERT_TRUE(composeCapturePreview(background, selection_image, selection,
                                    screen));
  EXPECT_EQ(background.pixels[0], 13u);
  EXPECT_EQ(background.pixels[1], 1u);
}

TEST(CapturePreviewTest, RejectsMalformedImageWithoutChangingBackground) {
  Image background{2, 2, {1u, 1u, 1u, 1u}};
  const Image original = background;
  Image malformed{2, 2, {9u}};
  ScreenPhysicalRect selection;
  selection.width = 2;
  selection.height = 2;

  EXPECT_FALSE(composeCapturePreview(
      background, malformed, selection, coord::VirtualScreenRect{0, 0, 2, 2}));
  EXPECT_EQ(background.pixels, original.pixels);

  background = original;
  const Image wrong_size{1, 2, {9u, 9u}};
  EXPECT_FALSE(composeCapturePreview(
      background, wrong_size, selection, coord::VirtualScreenRect{0, 0, 2, 2}));
  EXPECT_EQ(background.pixels, original.pixels);
}

}  // namespace qingying
