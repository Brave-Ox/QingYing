#include "qingying/overlay/coordinate_transform.hpp"

#include <gtest/gtest.h>

namespace qingying {
namespace coord {

TEST(CoordinateTransformTest, LogicalToPhysicalAt96IsIdentity) {
  EXPECT_EQ(logicalToPhysical(100, 96), 100);
  EXPECT_EQ(logicalToPhysical(-40, 96), -40);
}

TEST(CoordinateTransformTest, LogicalToPhysicalAt120Scales) {
  EXPECT_EQ(logicalToPhysical(100, 120), 125);  // 100 * 120 / 96
}

TEST(CoordinateTransformTest, LogicalToPhysicalAt144Scales) {
  EXPECT_EQ(logicalToPhysical(200, 144), 300);  // 200 * 144 / 96
}

TEST(CoordinateTransformTest, PhysicalToLogicalAt120Scales) {
  EXPECT_EQ(physicalToLogical(125, 120), 100);  // 125 * 96 / 120
}

TEST(CoordinateTransformTest, PhysicalToLogicalGuardsZeroDpi) {
  EXPECT_EQ(physicalToLogical(100, 0), 0);
}

TEST(CoordinateTransformTest, ClientToScreenAddsOrigin) {
  const VirtualScreenRect screen{-1920, -540, 3840, 1080};
  EXPECT_EQ(clientToScreenX(10, screen), -1910);
  EXPECT_EQ(clientToScreenY(20, screen), -520);
}

TEST(CoordinateTransformTest, ClientToScreenWithZeroOriginIsIdentity) {
  const VirtualScreenRect screen{0, 0, 1920, 1080};
  EXPECT_EQ(clientToScreenX(100, screen), 100);
  EXPECT_EQ(clientToScreenY(50, screen), 50);
}

TEST(CoordinateTransformTest, VirtualScreenRectBounds) {
  const VirtualScreenRect screen{-1920, 0, 3840, 1080};
  EXPECT_EQ(screen.right(), 1920);
  EXPECT_EQ(screen.bottom(), 1080);
}

TEST(CoordinateTransformTest, ClientRectConvertsToPhysicalScreenRect) {
  const VirtualScreenRect screen{-1920, -540, 3840, 1080};
  const OverlayClientRect client{10, 20, 640, 480};

  const ScreenPhysicalRect physical = clientToScreen(client, screen);
  EXPECT_EQ(physical.x, -1910);
  EXPECT_EQ(physical.y, -520);
  EXPECT_EQ(physical.width, 640);
  EXPECT_EQ(physical.height, 480);
}

TEST(CoordinateTransformTest, ScreenRectConvertsBackToOverlayClientRect) {
  const VirtualScreenRect screen{-1920, -540, 3840, 1080};
  const ScreenPhysicalRect physical{-1910, -520, 640, 480};

  const OverlayClientRect client = screenToClient(physical, screen);
  EXPECT_EQ(client.x, 10);
  EXPECT_EQ(client.y, 20);
  EXPECT_EQ(client.width, 640);
  EXPECT_EQ(client.height, 480);
}

TEST(CoordinateTransformTest, ScreenRectConvertsToDesktopImagePixels) {
  const VirtualScreenRect desktop{-1920, -540, 3840, 1080};
  const ScreenPhysicalRect selection{-1900, -500, 640, 480};

  const ImagePixelRect image_rect = screenToImage(selection, desktop);
  EXPECT_EQ(image_rect.x, 20);
  EXPECT_EQ(image_rect.y, 40);
  EXPECT_EQ(image_rect.width, 640);
  EXPECT_EQ(image_rect.height, 480);
  EXPECT_EQ(imageToScreen(image_rect, desktop).x, selection.x);
  EXPECT_EQ(imageToScreen(image_rect, desktop).y, selection.y);
}

}  // namespace coord
}  // namespace qingying
