#include <cstdint>

#include <Windows.h>

#include <gtest/gtest.h>

#include "qingying/action/image.hpp"
#include "qingying/window/smart_region_detector.hpp"
#include "known_content_locator.hpp"
#include "visual_region_locator.hpp"

namespace qingying {
namespace {

constexpr std::uint32_t kPagePixel = 0xFFF8F8F8u;
constexpr std::uint32_t kSidebarPixel = 0xFFE0E0E0u;
constexpr std::uint32_t kWeakCardPixel = 0xFFECECECu;
constexpr int kListRowLeft = 50;
constexpr int kListRowTop = 100;
constexpr int kListRowRight = 350;
constexpr int kListRowBottom = 160;
constexpr int kPhotoLeft = 80;
constexpr int kPhotoTop = 40;
constexpr int kPhotoRight = 320;
constexpr int kPhotoBottom = 260;
constexpr int kSmallControlLeft = 80;
constexpr int kSmallControlTop = 100;
constexpr int kSmallControlRight = 156;
constexpr int kSmallControlBottom = 130;

Image makeSidebarImage()
{
  Image image;
  image.width = 400;
  image.height = 300;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 50; y < image.height; ++y) {
    for (int x = 0; x < 120; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)] = kSidebarPixel;
    }
  }
  return image;
}

Image makeBoundedCardImage()
{
  Image image;
  image.width = 400;
  image.height = 300;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 60; y < 240; ++y) {
    for (int x = 80; x < 280; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)] = kSidebarPixel;
    }
  }
  return image;
}

Image makeBoundedListRowImage()
{
  Image image;
  image.width = 400;
  image.height = 300;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = kListRowTop; y < kListRowBottom; ++y) {
    for (int x = kListRowLeft; x < kListRowRight; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)] = kSidebarPixel;
    }
  }
  return image;
}

Image makeNarrowSidebarInWideOwnerImage()
{
  Image image;
  image.width = 1600;
  image.height = 900;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 120; y < 780; ++y) {
    for (int x = 40; x < 280; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)] = kSidebarPixel;
    }
  }
  return image;
}

Image makeOwnerAttachedSidebarImage()
{
  Image image;
  image.width = 1600;
  image.height = 900;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 120; y < 780; ++y) {
    for (int x = 0; x < 280; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)] = kSidebarPixel;
    }
  }
  return image;
}

Image makeShortRowInWideOwnerImage()
{
  Image image;
  image.width = 1600;
  image.height = 900;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 300; y < 346; ++y) {
    for (int x = 500; x < 900; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)] = kSidebarPixel;
    }
  }
  return image;
}

Image makeWideRowOutsideLocalSampleImage()
{
  Image image;
  image.width = 1600;
  image.height = 900;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 300; y < 346; ++y)
  {
    for (int x = 200; x < 1400; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = kSidebarPixel;
    }
  }
  return image;
}

Image makeCardClippedByImageBounds()
{
  Image image;
  image.width = 400;
  image.height = 300;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 60; y < 240; ++y)
  {
    for (int x = 0; x < 200; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = kSidebarPixel;
    }
  }
  return image;
}

Image makeWeakBoundaryCardImage()
{
  Image image;
  image.width = 400;
  image.height = 300;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 60; y < 240; ++y) {
    for (int x = 80; x < 280; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)] = kWeakCardPixel;
    }
  }
  return image;
}

Image makeTextGlyphImage()
{
  Image image;
  image.width = 400;
  image.height = 300;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 130; y < 146; ++y) {
    for (int x = 160; x < 176; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)] = 0xFF202020u;
    }
  }
  for (int y = 130; y < 146; ++y) {
    for (int x = 200; x < 216; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)] = 0xFF202020u;
    }
  }
  return image;
}

Image makeLargeOwnerAttachedWorkbenchImage()
{
  Image image;
  image.width = 1600;
  image.height = 900;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 40; y < 760; ++y)
  {
    for (int x = 0; x < 1140; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = kSidebarPixel;
    }
  }
  return image;
}

Image makeMediumOwnerAttachedWorkbenchImage()
{
  Image image;
  image.width = 1600;
  image.height = 900;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 40; y < 760; ++y)
  {
    for (int x = 0; x < 960; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = kSidebarPixel;
    }
  }
  return image;
}

Image makeOwnerWidthHorizontalBandImage()
{
  Image image;
  image.width = 400;
  image.height = 300;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 100; y < 160; ++y)
  {
    for (int x = 0; x < image.width; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = kSidebarPixel;
    }
  }
  return image;
}

Image makeOwnerHeightVerticalBandImage()
{
  Image image;
  image.width = 400;
  image.height = 300;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 0; y < image.height; ++y)
  {
    for (int x = 120; x < 200; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = kSidebarPixel;
    }
  }
  return image;
}

Image makeSmallRoundedControlImage(std::uint32_t background_pixel,
                                   std::uint32_t control_pixel)
{
  Image image;
  image.width = 240;
  image.height = 180;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      background_pixel);
  constexpr int CornerInset = 4;
  for (int y = kSmallControlTop; y < kSmallControlBottom; ++y)
  {
    for (int x = kSmallControlLeft; x < kSmallControlRight; ++x)
    {
      const bool inside_horizontal_center =
          x >= kSmallControlLeft + CornerInset &&
          x < kSmallControlRight - CornerInset;
      const bool inside_vertical_center =
          y >= kSmallControlTop + CornerInset &&
          y < kSmallControlBottom - CornerInset;
      if (inside_horizontal_center || inside_vertical_center)
      {
        image.pixels.at(static_cast<std::size_t>(y) *
                            static_cast<std::size_t>(image.width) +
                        static_cast<std::size_t>(x)) = control_pixel;
      }
    }
  }
  return image;
}

Image makeBookmarkLinkImage()
{
  Image image;
  image.width = 400;
  image.height = 180;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 72; y < 96; ++y)
  {
    for (int x = 100; x < 260; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = kSidebarPixel;
    }
  }
  return image;
}

Image makeCompactIconButtonImage()
{
  Image image;
  image.width = 240;
  image.height = 180;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  for (int y = 80; y < 100; ++y)
  {
    for (int x = 110; x < 130; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF303030u;
    }
  }
  return image;
}

Image makeTexturedPhotoWithInternalRoadEdges()
{
  Image image;
  image.width = 400;
  image.height = 300;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);

  for (int y = kPhotoTop; y < kPhotoBottom; ++y)
  {
    for (int x = kPhotoLeft; x < kPhotoRight; ++x)
    {
      const bool above_horizon = y < 140;
      const bool inside_road = x >= 150 && x < 250;
      const std::uint32_t texture =
          static_cast<std::uint32_t>(((x * 3 + y * 5) % 11) * 0x00010101u);
      const std::uint32_t base = inside_road
                                     ? (above_horizon ? 0xFF707070u
                                                      : 0xFF202020u)
                                     : (above_horizon ? 0xFFB07030u
                                                      : 0xFF205020u);
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(image.width) +
                   static_cast<std::size_t>(x)] = base + texture;
    }
  }
  return image;
}

Image makeWideRowWithLocallyVisibleRoundedTop()
{
  Image image;
  image.width = 1600;
  image.height = 900;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);

  for (int y = 250; y < 550; ++y)
  {
    const int left = y < 270 ? 700 : 200;
    const int right = y < 270 ? 900 : 1400;
    for (int x = left; x < right; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = kSidebarPixel;
    }
  }
  return image;
}

Image makeWorkbenchEditorAndTerminalImage()
{
  Image image;
  image.width = 2000;
  image.height = 1200;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      0xFF101010u);

  for (int y = 100; y < 700; ++y)
  {
    for (int x = 100; x < 1500; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF202020u;
    }
  }
  for (int y = 700; y < 1050; ++y)
  {
    for (int x = 100; x < 1500; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF303030u;
    }
  }
  return image;
}

Image makeWorkbenchTerminalWithHeaderImage()
{
  Image image;
  image.width = 2000;
  image.height = 1200;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      0xFF101010u);

  for (int y = 100; y < 694; ++y)
  {
    for (int x = 100; x < 1500; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF202020u;
    }
  }
  for (int y = 700; y < 764; ++y)
  {
    for (int x = 100; x < 1500; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF303030u;
    }
  }
  for (int y = 771; y < 1050; ++y)
  {
    for (int x = 100; x < 1500; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF303030u;
    }
  }
  return image;
}

Image makeWorkbenchTerminalWithPromptGapImage()
{
  Image image;
  image.width = 1920;
  image.height = 1032;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      0xFF101010u);

  for (int y = 129; y < 607; ++y)
  {
    for (int x = 302; x < 1399; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF202020u;
    }
  }
  for (int y = 613; y < 676; ++y)
  {
    for (int x = 302; x < 1399; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF303030u;
    }
  }
  for (int y = 744; y < 999; ++y)
  {
    for (int x = 302; x < 1399; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF303030u;
    }
  }
  return image;
}

Image makeTexturedWorkbenchLayoutImage()
{
  Image image;
  image.width = 1920;
  image.height = 1032;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      0xFF101010u);

  for (int y = 36; y < 999; ++y)
  {
    for (int x = 0; x < 302; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF181818u;
    }
    for (int x = 1399; x < image.width; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF242424u;
    }
  }

  for (int y = 84; y < 607; ++y)
  {
    for (int x = 302; x < 1399; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF303030u;
    }
  }
  for (int y = 613; y < 676; ++y)
  {
    for (int x = 302; x < 1399; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF282828u;
    }
  }
  for (int y = 744; y < 999; ++y)
  {
    for (int x = 302; x < 1399; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF282828u;
    }
  }

  for (int y = 84; y < 607; ++y)
  {
    image.pixels.at(static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(image.width) +
                    371) = 0xFF808080u;
  }

  for (int y = 744; y < 852; ++y)
  {
    image.pixels.at(static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(image.width) +
                    371) = 0xFF808080u;
  }

  constexpr int InternalHorizontalEdges[] = {795, 802, 897, 904};
  for (const int y : InternalHorizontalEdges)
  {
    for (int x = 302; x < 1399; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF808080u;
    }
  }

  for (int y = 108; y < 936; y += 36)
  {
    for (int x = 48; x < 280; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF484848u;
    }
  }
  for (int y = 96; y < 936; y += 48)
  {
    for (int x = 1420; x < 1880; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(x)) = 0xFF505050u;
    }
  }
  return image;
}

TEST(VisualRegionLocatorTest,
     RejectsCandidateWhenAnySideFallsBackToTheOwnerClientArea)
{
  const Image image = makeSidebarImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  EXPECT_FALSE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {48, 160}, result));
  EXPECT_TRUE(result.empty());
}

TEST(VisualRegionLocatorTest, RecordsAllVisualEdgesForABoundedCandidate)
{
  const Image image = makeBoundedCardImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;
  window_detail::VisualRegionDiagnostic diagnostic;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {160, 150}, result,
      diagnostic));
  EXPECT_TRUE(diagnostic.accepted);
  EXPECT_EQ(diagnostic.candidate.left, 80);
  EXPECT_EQ(diagnostic.candidate.top, 60);
  EXPECT_EQ(diagnostic.candidate.right, 280);
  EXPECT_EQ(diagnostic.candidate.bottom, 240);
  EXPECT_EQ(diagnostic.edge_mask,
            static_cast<std::uint8_t>(window_detail::VisualRegionEdge::Left) |
                static_cast<std::uint8_t>(window_detail::VisualRegionEdge::Right) |
                static_cast<std::uint8_t>(window_detail::VisualRegionEdge::Top) |
                static_cast<std::uint8_t>(window_detail::VisualRegionEdge::Bottom));
}

TEST(VisualRegionLocatorTest, FindsBoundedShortListRow)
{
  const Image image = makeBoundedListRowImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {200, 130}, result));
  EXPECT_EQ(result.left, kListRowLeft);
  EXPECT_EQ(result.top, kListRowTop);
  EXPECT_EQ(result.right, kListRowRight);
  EXPECT_EQ(result.bottom, kListRowBottom);
}

TEST(VisualRegionLocatorTest, FindsNarrowSidebarInsideWideOwner)
{
  const Image image = makeNarrowSidebarInWideOwnerImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {160, 400}, result));
  EXPECT_EQ(result.left, 40);
  EXPECT_EQ(result.top, 120);
  EXPECT_EQ(result.right, 280);
  EXPECT_EQ(result.bottom, 780);
}

TEST(VisualRegionLocatorTest, FindsSidebarAttachedToOneOwnerEdge)
{
  const Image image = makeOwnerAttachedSidebarImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {160, 400}, result));
  EXPECT_EQ(result.left, 0);
  EXPECT_EQ(result.top, 120);
  EXPECT_EQ(result.right, 280);
  EXPECT_EQ(result.bottom, 780);
}

TEST(VisualRegionLocatorTest,
     FindsOwnerWidthBandWithOnlyTopAndBottomVisualBoundaries)
{
  const Image image = makeOwnerWidthHorizontalBandImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {200, 130}, result));
  EXPECT_EQ(result.left, 0);
  EXPECT_EQ(result.top, 100);
  EXPECT_EQ(result.right, image.width);
  EXPECT_EQ(result.bottom, 160);
}

TEST(VisualRegionLocatorTest,
     FindsOwnerHeightBandWithOnlyLeftAndRightVisualBoundaries)
{
  const Image image = makeOwnerHeightVerticalBandImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {160, 150}, result));
  EXPECT_EQ(result.left, 120);
  EXPECT_EQ(result.top, 0);
  EXPECT_EQ(result.right, 200);
  EXPECT_EQ(result.bottom, image.height);
}

TEST(VisualRegionLocatorTest,
     RejectsOwnerAttachedCandidateWhenCompleteBoundariesAreRequired)
{
  const Image image = makeOwnerAttachedSidebarImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  EXPECT_FALSE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {160, 400}, result,
      window_detail::VisualRegionSearchPolicy::RequireCompleteBoundaries));
  EXPECT_TRUE(result.empty());
}

TEST(VisualRegionLocatorTest,
     WorkbenchPolicyRejectsLargeIncompleteCandidateSpanningPanels)
{
  const Image image = makeLargeOwnerAttachedWorkbenchImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  EXPECT_FALSE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {640, 400}, result,
      window_detail::VisualRegionSearchPolicy::RejectLargeIncompleteBoundaries));
  EXPECT_TRUE(result.empty());
}

TEST(VisualRegionLocatorTest,
     WorkbenchPolicyRejectsMediumIncompleteCandidateSpanningPanels)
{
  const Image image = makeMediumOwnerAttachedWorkbenchImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  EXPECT_FALSE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {640, 400}, result,
      window_detail::VisualRegionSearchPolicy::RejectLargeIncompleteBoundaries));
  EXPECT_TRUE(result.empty());
}

TEST(VisualRegionLocatorTest,
     WorkbenchPolicyKeepsSmallOwnerAttachedSidebar)
{
  const Image image = makeOwnerAttachedSidebarImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {160, 400}, result,
      window_detail::VisualRegionSearchPolicy::RejectLargeIncompleteBoundaries));
  EXPECT_EQ(result.left, 0);
  EXPECT_EQ(result.right, 280);
}

TEST(VisualRegionLocatorTest,
     WorkbenchPolicyFindsEditorAndTerminalBeyondLocalSearchDistance)
{
  const Image image = makeWorkbenchEditorAndTerminalImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect editor;
  WindowRect terminal;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 400}, editor,
      window_detail::VisualRegionSearchPolicy::RejectLargeIncompleteBoundaries));
  EXPECT_EQ(editor.left, 100);
  EXPECT_EQ(editor.top, 100);
  EXPECT_EQ(editor.right, 1500);
  EXPECT_EQ(editor.bottom, 700);

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 900}, terminal,
      window_detail::VisualRegionSearchPolicy::RejectLargeIncompleteBoundaries));
  EXPECT_EQ(terminal.left, 100);
  EXPECT_EQ(terminal.top, 700);
  EXPECT_EQ(terminal.right, 1500);
  EXPECT_EQ(terminal.bottom, 1050);
}

TEST(VisualRegionLocatorTest,
     ElectronWorkbenchPolicySeparatesEditorAndTerminalWithoutGenericSearch)
{
  const Image image = makeWorkbenchEditorAndTerminalImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect editor;
  WindowRect terminal;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 400}, editor,
      window_detail::VisualRegionSearchPolicy::ElectronWorkbench));
  EXPECT_EQ(editor.left, 100);
  EXPECT_EQ(editor.top, 100);
  EXPECT_EQ(editor.right, 1500);
  EXPECT_EQ(editor.bottom, 700);

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 900}, terminal,
      window_detail::VisualRegionSearchPolicy::ElectronWorkbench));
  EXPECT_EQ(terminal.left, 100);
  EXPECT_EQ(terminal.top, 700);
  EXPECT_EQ(terminal.right, 1500);
  EXPECT_EQ(terminal.bottom, 1050);
}

TEST(VisualRegionLocatorTest,
     ElectronWorkbenchPolicyMergesTerminalHeaderWithTerminalBody)
{
  const Image image = makeWorkbenchTerminalWithHeaderImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect header;
  WindowRect body;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 730}, header,
      window_detail::VisualRegionSearchPolicy::ElectronWorkbench));
  EXPECT_EQ(header.left, 100);
  EXPECT_EQ(header.top, 700);
  EXPECT_EQ(header.right, 1500);
  EXPECT_EQ(header.bottom, 1050);

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 900}, body,
      window_detail::VisualRegionSearchPolicy::ElectronWorkbench));
  EXPECT_EQ(body.left, 100);
  EXPECT_EQ(body.top, 700);
  EXPECT_EQ(body.right, 1500);
  EXPECT_EQ(body.bottom, 1050);
}

TEST(VisualRegionLocatorTest,
     ElectronWorkbenchPolicyMergesTerminalAcrossPromptGap)
{
  const Image image = makeWorkbenchTerminalWithPromptGapImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect header;
  WindowRect prompt_gap;
  WindowRect body;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 640}, header,
      window_detail::VisualRegionSearchPolicy::ElectronWorkbench));
  EXPECT_EQ(header.left, 302);
  EXPECT_EQ(header.top, 613);
  EXPECT_EQ(header.right, 1399);
  EXPECT_EQ(header.bottom, 999);

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 700}, prompt_gap,
      window_detail::VisualRegionSearchPolicy::ElectronWorkbench));
  EXPECT_EQ(prompt_gap.left, 302);
  EXPECT_EQ(prompt_gap.top, 613);
  EXPECT_EQ(prompt_gap.right, 1399);
  EXPECT_EQ(prompt_gap.bottom, 999);

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 800}, body,
      window_detail::VisualRegionSearchPolicy::ElectronWorkbench));
  EXPECT_EQ(body.left, 302);
  EXPECT_EQ(body.top, 613);
  EXPECT_EQ(body.right, 1399);
  EXPECT_EQ(body.bottom, 999);
}

TEST(VisualRegionLocatorTest,
     ElectronWorkbenchLayoutMergesTexturedTerminalIntoOnePanel)
{
  const Image image = makeTexturedWorkbenchLayoutImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  constexpr POINT TerminalPoints[] = {{800, 650}, {800, 811}, {800, 940}};

  for (const POINT point : TerminalPoints)
  {
    WindowRect result;
    ASSERT_TRUE(window_detail::findVisualRegion(
        image, image_screen_rect, owner_client_rect, point, result,
        window_detail::VisualRegionSearchPolicy::ElectronWorkbench));
    EXPECT_EQ(result.left, 302);
    EXPECT_EQ(result.top, 613);
    EXPECT_EQ(result.right, 1399);
    EXPECT_EQ(result.bottom, 999);
  }
}

TEST(VisualRegionLocatorTest,
     ElectronWorkbenchLayoutKeepsTexturedSidePanelsIndependent)
{
  const Image image = makeTexturedWorkbenchLayoutImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect left_panel;
  WindowRect right_panel;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {180, 400}, left_panel,
      window_detail::VisualRegionSearchPolicy::ElectronWorkbench));
  EXPECT_EQ(left_panel.left, 0);
  EXPECT_EQ(left_panel.top, 36);
  EXPECT_EQ(left_panel.right, 302);
  EXPECT_EQ(left_panel.bottom, 999);

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {1600, 400}, right_panel,
      window_detail::VisualRegionSearchPolicy::ElectronWorkbench));
  EXPECT_EQ(right_panel.left, 1399);
  EXPECT_EQ(right_panel.top, 36);
  EXPECT_EQ(right_panel.right, 1920);
  EXPECT_EQ(right_panel.bottom, 999);
}

TEST(VisualRegionLocatorTest,
     IdentifiesWholeClientElectronRendererAsGenericWorkbenchContainer)
{
  const WindowRect client_rect{0, 0, 1920, 1032};

  EXPECT_TRUE(window_detail::electronWorkbenchRendererCoversClientArea(
      client_rect, {0, 0, 1920, 1032}));
  EXPECT_FALSE(window_detail::electronWorkbenchRendererCoversClientArea(
      client_rect, {302, 129, 1399, 607}));
}

TEST(VisualRegionLocatorTest,
     KeepsFullyBoundedCandidateWhenCompleteBoundariesAreRequired)
{
  const Image image = makeBoundedCardImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {160, 150}, result,
      window_detail::VisualRegionSearchPolicy::RequireCompleteBoundaries));
  EXPECT_EQ(result.left, 80);
  EXPECT_EQ(result.top, 60);
  EXPECT_EQ(result.right, 280);
  EXPECT_EQ(result.bottom, 240);
}

TEST(VisualRegionLocatorTest, FindsShortRowInsideWideOwner)
{
  const Image image = makeShortRowInWideOwnerImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {700, 323}, result));
  EXPECT_EQ(result.left, 500);
  EXPECT_EQ(result.top, 300);
  EXPECT_EQ(result.right, 900);
  EXPECT_EQ(result.bottom, 346);
}

TEST(VisualRegionLocatorTest,
     FindsWideRowWhoseSidesAreOutsideLocalSampleBounds)
{
  const Image image = makeWideRowOutsideLocalSampleImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 323}, result));
  EXPECT_EQ(result.left, 200);
  EXPECT_EQ(result.top, 300);
  EXPECT_EQ(result.right, 1400);
  EXPECT_EQ(result.bottom, 346);
}

TEST(VisualRegionLocatorTest, RejectsImageClipAsMissingOwnerBoundary)
{
  const Image image = makeCardClippedByImageBounds();
  const WindowRect image_screen_rect{100, 0, 500, image.height};
  const WindowRect owner_client_rect{0, 0, 500, image.height};
  WindowRect result;

  EXPECT_FALSE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {160, 150}, result));
  EXPECT_TRUE(result.empty());
}

TEST(VisualRegionLocatorTest, FindsCardWithWeakButContinuousBoundaries)
{
  const Image image = makeWeakBoundaryCardImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {160, 150}, result));
  EXPECT_EQ(result.left, 80);
  EXPECT_EQ(result.top, 60);
  EXPECT_EQ(result.right, 280);
  EXPECT_EQ(result.bottom, 240);
}

TEST(VisualRegionLocatorTest, RejectsTextGlyphsAsRegionBoundaries)
{
  const Image image = makeTextGlyphImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  EXPECT_FALSE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {168, 138}, result));
  EXPECT_TRUE(result.empty());
}

TEST(VisualRegionLocatorTest, FindsBookmarkLinkSizedVisualFixture)
{
  const Image image = makeBookmarkLinkImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {180, 84}, result));
  EXPECT_EQ(result.left, 100);
  EXPECT_EQ(result.top, 72);
  EXPECT_EQ(result.right, 260);
  EXPECT_EQ(result.bottom, 96);
}

TEST(VisualRegionLocatorTest,
     RejectsTwentyPixelIconFixtureWithoutAccessibilitySemantics)
{
  const Image image = makeCompactIconButtonImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  EXPECT_FALSE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {120, 90}, result));
  EXPECT_TRUE(result.empty());
}

TEST(VisualRegionLocatorTest,
     FindsSmallRoundedWeakContrastControlOnLightBackground)
{
  constexpr std::uint32_t LightControlPixel = 0xFFF2F2F2u;
  const Image image = makeSmallRoundedControlImage(
      kPagePixel, LightControlPixel);
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {118, 115}, result));
  EXPECT_EQ(result.left, kSmallControlLeft);
  EXPECT_EQ(result.top, kSmallControlTop);
  EXPECT_EQ(result.right, kSmallControlRight);
  EXPECT_EQ(result.bottom, kSmallControlBottom);
}

TEST(VisualRegionLocatorTest,
     FindsSmallRoundedWeakContrastControlOnDarkBackground)
{
  constexpr std::uint32_t DarkBackgroundPixel = 0xFF202020u;
  constexpr std::uint32_t DarkControlPixel = 0xFF262626u;
  const Image image = makeSmallRoundedControlImage(
      DarkBackgroundPixel, DarkControlPixel);
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {118, 115}, result));
  EXPECT_EQ(result.left, kSmallControlLeft);
  EXPECT_EQ(result.top, kSmallControlTop);
  EXPECT_EQ(result.right, kSmallControlRight);
  EXPECT_EQ(result.bottom, kSmallControlBottom);
}

TEST(VisualRegionLocatorTest,
     SmallRoundedControlProducesASelectableVisualCandidate)
{
  constexpr std::uint32_t LightControlPixel = 0xFFF2F2F2u;
  const Image image = makeSmallRoundedControlImage(
      kPagePixel, LightControlPixel);
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::findVisualRegionCandidate(
      image, image_screen_rect, owner_client_rect, {118, 115}, 1,
      candidate, nullptr));
  EXPECT_EQ(candidate.rect.left, kSmallControlLeft);
  EXPECT_EQ(candidate.rect.top, kSmallControlTop);
  EXPECT_EQ(candidate.rect.right, kSmallControlRight);
  EXPECT_EQ(candidate.rect.bottom, kSmallControlBottom);
  EXPECT_GE(candidate.visual_confidence, 70);
}

TEST(VisualRegionLocatorTest, RejectsUniformBackground)
{
  Image image;
  image.width = 400;
  image.height = 300;
  image.pixels.assign(static_cast<std::size_t>(image.width) *
                          static_cast<std::size_t>(image.height),
                      kPagePixel);
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  EXPECT_FALSE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {48, 160}, result));
  EXPECT_TRUE(result.empty());
}

TEST(VisualRegionLocatorTest,
     PrefersPhotoOuterBoundsOverInternalRoadAndHorizonEdges)
{
  const Image image = makeTexturedPhotoWithInternalRoadEdges();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {200, 180}, result));
  EXPECT_EQ(result.left, kPhotoLeft);
  EXPECT_EQ(result.top, kPhotoTop);
  EXPECT_EQ(result.right, kPhotoRight);
  EXPECT_EQ(result.bottom, kPhotoBottom);
}

TEST(VisualRegionLocatorTest,
     KeepsWideRegionWhoseBoundaryIsOnlyVisibleNearPointer)
{
  const Image image = makeWideRowWithLocallyVisibleRoundedTop();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  WindowRect result;

  ASSERT_TRUE(window_detail::findVisualRegion(
      image, image_screen_rect, owner_client_rect, {800, 400}, result));
  EXPECT_EQ(result.left, 200);
  EXPECT_EQ(result.top, 250);
  EXPECT_EQ(result.right, 1400);
  EXPECT_EQ(result.bottom, 550);
}

TEST(VisualRegionLocatorTest,
     VisualCandidateKeepsConfidenceWithoutDiagnosticOutput)
{
  const Image image = makeBoundedCardImage();
  const WindowRect image_screen_rect{0, 0, image.width, image.height};
  const WindowRect owner_client_rect{0, 0, image.width, image.height};
  SmartRegionCandidate candidate;

  ASSERT_TRUE(window_detail::findVisualRegionCandidate(
      image, image_screen_rect, owner_client_rect, {160, 150}, 1,
      candidate, nullptr));
  EXPECT_EQ(candidate.source, SmartRegionDiagnosticSource::Visual);
  EXPECT_EQ(candidate.rect.left, 80);
  EXPECT_GE(candidate.visual_confidence, 70);
}

}  // namespace
}  // namespace qingying
