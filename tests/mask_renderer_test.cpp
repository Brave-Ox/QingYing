#include "qingying/overlay/mask_renderer.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace qingying {
namespace mask {

namespace {

constexpr std::uint32_t kMakeBgra(std::uint8_t b, std::uint8_t g,
                                  std::uint8_t r, std::uint8_t a) {
  return (static_cast<std::uint32_t>(a) << 24) |
         (static_cast<std::uint32_t>(r) << 16) |
         (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
}

// 半透明遮罩层：约 35% 不透明黑（premultiplied：alpha 0x59，RGB 0x00）。
constexpr std::uint32_t kMaskPx = kMakeBgra(0, 0, 0, 0x59);
// 选区内部：视觉透明（alpha=1，保持分层窗口可命中以支持移动选区）。
constexpr std::uint32_t kClearPx = 0x01000000u;
// 选区边框：不透明亮橙，4px（alpha 0xFF，RGB 0xFF8000）。
constexpr std::uint32_t kBorderPx = kMakeBgra(0x00, 0x80, 0xFF, 0xFF);

std::uint32_t kPxAt(const std::vector<std::uint32_t>& px, int width, int x,
                    int y) {
  return px.at(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x));
}

}  // namespace

TEST(MaskRendererTest, FullScreenSelectionUnmaskedInterior) {
  SelectionResult sel{};
  sel.cancelled = false;
  sel.x = 0;
  sel.y = 0;
  sel.width = 100;
  sel.height = 80;

  std::vector<std::uint32_t> px;
  renderFullscreenMask(100, 80, sel, px);

  ASSERT_EQ(px.size(), 100u * 80u);
  // 选区覆盖全屏：内部无遮罩；选区顶点与边缘（4px 边框）为亮色。
  EXPECT_EQ(kPxAt(px, 100, 50, 40), kClearPx);
  EXPECT_EQ(kPxAt(px, 100, 3, 40), kBorderPx);  // 左边框最内一列 x=3
  EXPECT_EQ(kPxAt(px, 100, 4, 40), kClearPx);   // x=4 已是内部（边框 4 列像素）
  EXPECT_EQ(kPxAt(px, 100, 0, 0), kBorderPx);
  EXPECT_EQ(kPxAt(px, 100, 99, 79), kBorderPx);
}

TEST(MaskRendererTest, SelectionInteriorIsClear) {
  SelectionResult sel{};
  sel.cancelled = false;
  sel.x = 10;
  sel.y = 20;
  sel.width = 50;
  sel.height = 30;

  std::vector<std::uint32_t> px;
  renderFullscreenMask(100, 80, sel, px);

  // 边框内缘以内的区域才 clear；选区顶点/边界是边框，不在这里断言。
  EXPECT_EQ(kPxAt(px, 100, 20, 25), kClearPx);
  EXPECT_EQ(kPxAt(px, 100, 55, 45), kClearPx);
}

TEST(MaskRendererTest, OutsideSelectionIsMasked) {
  SelectionResult sel{};
  sel.cancelled = false;
  sel.x = 10;
  sel.y = 20;
  sel.width = 50;
  sel.height = 30;

  std::vector<std::uint32_t> px;
  renderFullscreenMask(100, 80, sel, px);

  EXPECT_EQ(kPxAt(px, 100, 0, 0), kMaskPx);
  EXPECT_EQ(kPxAt(px, 100, 99, 79), kMaskPx);
  EXPECT_EQ(kPxAt(px, 100, 60, 50), kMaskPx);
}

TEST(MaskRendererTest, BorderIsHighlighted) {
  SelectionResult sel{};
  sel.cancelled = false;
  sel.x = 10;
  sel.y = 20;
  sel.width = 50;
  sel.height = 30;

  std::vector<std::uint32_t> px;
  renderFullscreenMask(100, 80, sel, px);

  // 边框画在选区内缘：顶边、左边、右边、底边各取一像素。
  EXPECT_EQ(kPxAt(px, 100, 10, 20), kBorderPx);   // 左上顶点
  EXPECT_EQ(kPxAt(px, 100, 10, 21), kBorderPx);   // 顶边内一行
  EXPECT_EQ(kPxAt(px, 100, 11, 20), kBorderPx);   // 左边内一列
  EXPECT_EQ(kPxAt(px, 100, 59, 20), kBorderPx);   // 右上顶点
  EXPECT_EQ(kPxAt(px, 100, 10, 49), kBorderPx);   // 左下顶点
  EXPECT_EQ(kPxAt(px, 100, 59, 49), kBorderPx);   // 右下顶点
}

TEST(MaskRendererTest, SelectionPartiallyOffscreenIsClamped) {
  SelectionResult sel{};
  sel.cancelled = false;
  sel.x = -10;  // 选区左越界
  sel.y = -10;
  sel.width = 20;
  sel.height = 20;

  std::vector<std::uint32_t> px;
  renderFullscreenMask(100, 80, sel, px);

  // 与屏幕相交于 [0,0]..[9,9]：越界部分钳制到屏幕内，绘制可见部分。
  EXPECT_EQ(kPxAt(px, 100, 0, 0), kBorderPx);  // 可见部分左上顶点
  EXPECT_EQ(kPxAt(px, 100, 9, 9), kBorderPx);  // 可见部分右下顶点
  EXPECT_EQ(kPxAt(px, 100, 50, 40), kMaskPx);  // 其余仍为遮罩
}

TEST(MaskRendererTest, CancelledSelectionIsFullMask) {
  SelectionResult sel{};  // cancelled == true
  sel.width = 100;
  sel.height = 80;

  std::vector<std::uint32_t> px;
  renderFullscreenMask(100, 80, sel, px);

  EXPECT_EQ(px.front(), kMaskPx);
  EXPECT_EQ(px.back(), kMaskPx);
}

}  // namespace mask
}  // namespace qingying
