#include "qingying/overlay/overlay_renderer.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

namespace qingying {
namespace {

constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
constexpr std::uint32_t kMask = 0x59000000u;
constexpr std::uint32_t kHover = 0xFF00B4FFu;
constexpr std::uint32_t kHoverGlow = 0x70204C70u;
constexpr std::uint32_t kHoverBackground = 0xFF3864A0u;
constexpr std::uint32_t kHoverRevealedBackground = 0xFF37639Fu;

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

struct PixelBounds {
  int left{0};
  int top{0};
  int right{0};
  int bottom{0};
  bool found{false};
};

PixelBounds findPixelBounds(const std::vector<std::uint32_t>& pixels,
                            int width, int height,
                            std::uint32_t color) {
  PixelBounds bounds;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (pixelAt(pixels, width, x, y) != color) {
        continue;
      }
      if (!bounds.found) {
        bounds = {x, y, x + 1, y + 1, true};
      } else {
        bounds.left = (std::min)(bounds.left, x);
        bounds.top = (std::min)(bounds.top, y);
        bounds.right = (std::max)(bounds.right, x + 1);
        bounds.bottom = (std::max)(bounds.bottom, y + 1);
      }
    }
  }
  return bounds;
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
  OverlayClientRect selection{};
  selection.x = 4;
  selection.y = 4;
  selection.width = 12;
  selection.height = 12;
  OverlayClientRect hover{};
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
  OverlayClientRect selection{};
  OverlayClientRect hover{};
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

TEST(OverlayRendererTest, RendersGlowOutsideHoverBoundary) {
  OverlayClientRect selection{};
  OverlayClientRect hover{};
  hover.x = 5;
  hover.y = 6;
  hover.width = 6;
  hover.height = 5;
  Image background;
  Image preview;
  const OverlayRenderState state(selection, hover, background, preview,
                                 OverlayPhase::Sniffing, false, 0, true,
                                 false);

  std::vector<std::uint32_t> pixels;
  ASSERT_TRUE(OverlayRenderer::renderPixels(20, 20, state, pixels));
  EXPECT_EQ(pixelAt(pixels, 20, 4, 6), kHoverGlow);
  EXPECT_EQ(pixelAt(pixels, 20, 5, 6), kHover);
}

TEST(OverlayRendererTest, RevealsOriginalBackgroundInsideHoverRegion) {
  OverlayClientRect selection{};
  OverlayClientRect hover{};
  hover.x = 20;
  hover.y = 20;
  hover.width = 40;
  hover.height = 30;
  const Image background = makeSolidImage(100, 100, kHoverBackground);
  Image preview;
  const OverlayRenderState state(selection, hover, background, preview,
                                 OverlayPhase::Sniffing, false, 0, true,
                                 false);

  std::vector<std::uint32_t> pixels;
  ASSERT_TRUE(OverlayRenderer::renderPixels(100, 100, state, pixels));
  EXPECT_EQ(pixelAt(pixels, 100, 30, 30), kHoverRevealedBackground);
  EXPECT_EQ(pixelAt(pixels, 100, 20, 20), kHover);
}

TEST(OverlayRendererTest, DoesNotRenderHoverDuringCapturePassthrough) {
  OverlayClientRect selection{};
  selection.x = 20;
  selection.y = 20;
  selection.width = 40;
  selection.height = 30;
  const OverlayClientRect hover = selection;
  const Image background = makeSolidImage(100, 100, kWhite);
  Image preview;
  const OverlayRenderState state(selection, hover, background, preview,
                                 OverlayPhase::LongShotRunning, false, 0,
                                 true, true);

  std::vector<std::uint32_t> pixels;
  ASSERT_TRUE(OverlayRenderer::renderPixels(100, 100, state, pixels));
  EXPECT_EQ(pixelAt(pixels, 100, 30, 30), 0x00000000u);
  EXPECT_EQ(pixelAt(pixels, 100, 20, 54), kMask);
}

TEST(OverlayRendererTest, PassthroughClearsSelectionWithoutBackgroundCompose) {
  OverlayClientRect selection{};
  selection.x = 2;
  selection.y = 2;
  selection.width = 6;
  selection.height = 6;
  OverlayClientRect hover{};
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

TEST(OverlayRendererTest, KeepsLongShotPreviewOutsideCaptureSelection) {
  OverlayClientRect selection{};
  selection.x = 10;
  selection.y = 10;
  selection.width = 60;
  selection.height = 60;
  OverlayClientRect hover{};
  const Image background = makeSolidImage(100, 100, kWhite);
  const Image preview = makeSolidImage(10, 10, 0xFFCC0000u);
  const OverlayRenderState state(selection, hover, background, preview,
                                 OverlayPhase::LongShotRunning, false, 0,
                                 false, true);

  std::vector<std::uint32_t> pixels;
  ASSERT_TRUE(OverlayRenderer::renderPixels(100, 100, state, pixels));

  const PixelBounds preview_bounds =
      findPixelBounds(pixels, 100, 100, 0xFFCC0000u);
  ASSERT_TRUE(preview_bounds.found);
  EXPECT_TRUE(preview_bounds.right <= selection.x ||
              preview_bounds.left >= selection.x + selection.width ||
              preview_bounds.bottom <= selection.y ||
              preview_bounds.top >= selection.y + selection.height);
}

TEST(OverlayRendererTest, KeepsLongShotPreviewPanelAtStablePosition) {
  OverlayClientRect selection{};
  selection.x = 100;
  selection.y = 100;
  selection.width = 400;
  selection.height = 800;
  OverlayClientRect hover{};
  const Image background = makeSolidImage(1200, 1000, kWhite);
  const Image first_preview = makeSolidImage(440, 400, 0xFFCC0000u);
  const Image taller_preview = makeSolidImage(440, 720, 0xFF0000CCu);
  const OverlayRenderState first_state(selection, hover, background,
                                       first_preview,
                                       OverlayPhase::LongShotRunning, false,
                                       0, false, true);
  const OverlayRenderState taller_state(selection, hover, background,
                                        taller_preview,
                                        OverlayPhase::LongShotRunning, false,
                                        0, false, true);

  std::vector<std::uint32_t> first_pixels;
  std::vector<std::uint32_t> taller_pixels;
  ASSERT_TRUE(
      OverlayRenderer::renderPixels(1200, 1000, first_state, first_pixels));
  ASSERT_TRUE(
      OverlayRenderer::renderPixels(1200, 1000, taller_state, taller_pixels));

  constexpr std::uint32_t kPanelBorder = 0xFFFF8000u;
  const PixelBounds first_panel =
      findPixelBounds(first_pixels, 1200, 1000, kPanelBorder);
  const PixelBounds taller_panel =
      findPixelBounds(taller_pixels, 1200, 1000, kPanelBorder);
  ASSERT_TRUE(first_panel.found);
  ASSERT_TRUE(taller_panel.found);
  EXPECT_EQ(first_panel.left, taller_panel.left);
  EXPECT_EQ(first_panel.top, taller_panel.top);
  EXPECT_EQ(first_panel.right, taller_panel.right);
  EXPECT_EQ(first_panel.bottom, taller_panel.bottom);
}

}  // namespace qingying
