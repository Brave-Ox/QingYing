#include "qingying/overlay/overlay_renderer.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace qingying {
namespace {

constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
constexpr std::uint32_t kMask = 0x59000000u;
constexpr std::uint32_t kHover = 0xFF00B4FFu;

Image makeSolidImage(int width, int height, std::uint32_t color) {
  Image image;
  image.width = width;
  image.height = height;
  image.pixels.assign(static_cast<std::size_t>(width) *
                          static_cast<std::size_t>(height),
                      color);
  return image;
}

std::uint32_t pixelAt(const std::vector<std::uint32_t>& pixels, int width,
                      int x, int y) {
  return pixels.at(static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x));
}

}  // namespace

TEST(OverlayRendererTest, EmptyLongShotPreviewStaysEmpty) {
  EXPECT_TRUE(OverlayRenderer::makeLongShotPreviewImage(Image{}).empty());
}

TEST(OverlayRendererTest, LongShotPreviewIsBoundedAndNearestNeighbour) {
  Image source;
  source.width = 880;
  source.height = 4;
  source.pixels.resize(static_cast<std::size_t>(source.width) *
                       static_cast<std::size_t>(source.height));
  for (int y = 0; y < source.height; ++y) {
    for (int x = 0; x < source.width; ++x) {
      source.pixels[static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(source.width) +
                    static_cast<std::size_t>(x)] =
          0xFF000000u | static_cast<std::uint32_t>(x & 0xFF) |
          (static_cast<std::uint32_t>(y) << 8);
    }
  }

  const Image preview = OverlayRenderer::makeLongShotPreviewImage(source);

  ASSERT_EQ(preview.width, 440);
  ASSERT_EQ(preview.height, 2);
  EXPECT_EQ(preview.pixels.front(), source.pixels.front());
  EXPECT_EQ(preview.pixels.at(1), source.pixels.at(2));
  EXPECT_EQ(preview.pixels.at(static_cast<std::size_t>(preview.width)),
            source.pixels.at(static_cast<std::size_t>(source.width) * 2));
}

TEST(OverlayRendererTest, RendersSelectionHandlesIntoPixelFrame) {
  SelectionResult selection{};
  selection.cancelled = false;
  selection.x = 4;
  selection.y = 4;
  selection.width = 12;
  selection.height = 12;
  SelectionResult hover{};
  Image background;
  Image preview;
  const OverlayRenderState state(selection, hover, background, preview,
                                 OverlayPhase::Selected, true, 1, false,
                                 false);

  std::vector<std::uint32_t> pixels;
  ASSERT_TRUE(OverlayRenderer::renderPixels(20, 20, state, pixels));
  ASSERT_EQ(pixels.size(), 400u);
  EXPECT_EQ(pixelAt(pixels, 20, 4, 4), kWhite);
  EXPECT_EQ(pixelAt(pixels, 20, 10, 10), 0x01000000u);
  EXPECT_EQ(pixelAt(pixels, 20, 0, 0), kMask);
}

TEST(OverlayRendererTest, RendersHoverOutlineWhenRequested) {
  SelectionResult selection{};
  SelectionResult hover{};
  hover.cancelled = false;
  hover.x = 5;
  hover.y = 6;
  hover.width = 6;
  hover.height = 5;
  Image background;
  Image preview;
  const OverlayRenderState state(selection, hover, background, preview,
                                 OverlayPhase::Sniffing, false, 0, true, false);

  std::vector<std::uint32_t> pixels;
  ASSERT_TRUE(OverlayRenderer::renderPixels(20, 20, state, pixels));
  EXPECT_EQ(pixelAt(pixels, 20, 5, 6), kHover);
  EXPECT_EQ(pixelAt(pixels, 20, 12, 12), kMask);
}

TEST(OverlayRendererTest, PassthroughClearsSelectionWithoutBackgroundCompose) {
  SelectionResult selection{};
  selection.cancelled = false;
  selection.x = 2;
  selection.y = 2;
  selection.width = 6;
  selection.height = 6;
  SelectionResult hover{};
  const Image background = makeSolidImage(12, 12, kWhite);
  Image preview;
  const OverlayRenderState state(selection, hover, background, preview,
                                 OverlayPhase::LongShotRunning, false, 0,
                                 false, true);

  std::vector<std::uint32_t> pixels;
  ASSERT_TRUE(OverlayRenderer::renderPixels(12, 12, state, pixels));
  EXPECT_EQ(pixelAt(pixels, 12, 4, 4), 0x00000000u);
  EXPECT_EQ(pixelAt(pixels, 12, 0, 0), kMask);
}

}  // namespace qingying
