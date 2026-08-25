#include "qingying/overlay/selection_handles.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace qingying {
namespace handles {

TEST(SelectionHandlesTest, CornersHitCorrectHandles) {
  const int sx = 100;
  const int sy = 80;
  const int sw = 200;
  const int sh = 150;
  EXPECT_EQ(hitTest(100, 80, sx, sy, sw, sh), SelectionHandle::TopLeft);
  EXPECT_EQ(hitTest(299, 80, sx, sy, sw, sh), SelectionHandle::TopRight);
  EXPECT_EQ(hitTest(100, 229, sx, sy, sw, sh), SelectionHandle::BottomLeft);
  EXPECT_EQ(hitTest(299, 229, sx, sy, sw, sh), SelectionHandle::BottomRight);
}

TEST(SelectionHandlesTest, EdgesHitCorrectHandles) {
  const int sx = 100;
  const int sy = 80;
  const int sw = 200;
  const int sh = 150;
  EXPECT_EQ(hitTest(200, 80, sx, sy, sw, sh), SelectionHandle::Top);
  EXPECT_EQ(hitTest(200, 229, sx, sy, sw, sh), SelectionHandle::Bottom);
  EXPECT_EQ(hitTest(100, 155, sx, sy, sw, sh), SelectionHandle::Left);
  EXPECT_EQ(hitTest(299, 155, sx, sy, sw, sh), SelectionHandle::Right);
}

TEST(SelectionHandlesTest, InteriorReturnsMove) {
  EXPECT_EQ(hitTest(150, 120, 100, 80, 200, 150), SelectionHandle::Move);
}

TEST(SelectionHandlesTest, OutsideReturnsNone) {
  EXPECT_EQ(hitTest(50, 50, 100, 80, 200, 150), SelectionHandle::None);
  EXPECT_EQ(hitTest(400, 300, 100, 80, 200, 150), SelectionHandle::None);
}

TEST(SelectionHandlesTest, CornerTakesPrecedenceOverEdge) {
  // 左上角：既贴近左边也贴近上边，应优先返回角手柄。
  EXPECT_EQ(hitTest(101, 81, 100, 80, 200, 150), SelectionHandle::TopLeft);
}

TEST(SelectionHandlesTest, InvalidSelectionReturnsNone) {
  EXPECT_EQ(hitTest(100, 80, 100, 80, 0, 0), SelectionHandle::None);
}

TEST(SelectionHandlesTest, DrawHandlesMarksHandleCenters) {
  std::vector<std::uint32_t> px(200 * 150, 0u);
  drawHandles(px, 200, 150, 100, 50, 60, 40, 0xFFFFFFFFu, 2);

  const auto at = [&](int x, int y) {
    return px.at(static_cast<std::size_t>(y) * 200u +
                 static_cast<std::size_t>(x));
  };
  // 四角中心被着色。
  EXPECT_EQ(at(100, 50), 0xFFFFFFFFu);
  EXPECT_EQ(at(159, 50), 0xFFFFFFFFu);
  EXPECT_EQ(at(100, 89), 0xFFFFFFFFu);
  EXPECT_EQ(at(159, 89), 0xFFFFFFFFu);
}

TEST(SelectionHandlesTest, DrawHandlesDoesNotTouchFarPixels) {
  std::vector<std::uint32_t> px(200 * 150, 0u);
  drawHandles(px, 200, 150, 100, 50, 60, 40, 0xFFFFFFFFu, 2);

  const auto at = [&](int x, int y) {
    return px.at(static_cast<std::size_t>(y) * 200u +
                 static_cast<std::size_t>(x));
  };
  EXPECT_EQ(at(0, 0), 0u);
  EXPECT_EQ(at(199, 149), 0u);
  EXPECT_EQ(at(150, 75), 0u);  // 选区内部，远离边框/手柄
}

}  // namespace handles
}  // namespace qingying
