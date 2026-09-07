#include <cstdint>

#include <Windows.h>

#include <gtest/gtest.h>

#include "qingying/action/image.hpp"
#include "qingying/window/smart_region_detector.hpp"
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

}  // namespace
}  // namespace qingying
