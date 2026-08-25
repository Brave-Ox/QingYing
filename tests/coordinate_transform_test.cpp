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

}  // namespace coord
}  // namespace qingying
