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

TEST(MaskRendererTest, ComposeBackgroundDimsMaskOutsideSelection) {
  // 背景 20x20 纯白（不透明）。遮罩：10x10 选区外的遮罩像素为 kMaskPx。
  Image bg;
  bg.width = 20;
  bg.height = 20;
  bg.pixels.assign(20u * 20u, kMakeBgra(255, 255, 255, 255));

  SelectionResult sel{};
  sel.cancelled = false;
  sel.x = 5;
  sel.y = 5;
  sel.width = 10;
  sel.height = 10;

  std::vector<std::uint32_t> mask_px;
  renderFullscreenMask(20, 20, sel, mask_px);
  ASSERT_EQ(mask_px.size(), 20u * 20u);
  ASSERT_EQ(kPxAt(mask_px, 20, 0, 0), kMaskPx);  // 选区外确认

  std::vector<std::uint32_t> out;
  ASSERT_TRUE(mask::composeBackground(bg, mask_px, out));
  ASSERT_EQ(out.size(), 20u * 20u);

  // 选区外像素：白 * (1 - 0x59/255) ≈ 白 * 0.65，alpha 恒不透明。
  const std::uint32_t dimmed =
      kMakeBgra(static_cast<std::uint8_t>(255u * (255u - 0x59u) / 255u),
                static_cast<std::uint8_t>(255u * (255u - 0x59u) / 255u),
                static_cast<std::uint8_t>(255u * (255u - 0x59u) / 255u), 255);
  EXPECT_EQ(kPxAt(out, 20, 0, 0), dimmed);
  EXPECT_EQ(kPxAt(out, 20, 19, 19), dimmed);
  EXPECT_EQ(kPxAt(out, 20, 0, 0) >> 24, 0xFFu);  // 合成输出恒为不透明
}

TEST(MaskRendererTest, ComposeBackgroundKeepsSelectionInterior) {
  // 背景 20x20 填充 (B,G,R)=(10,20,30)；选区 5,5,10,10 的内部像素保持原色。
  Image bg;
  bg.width = 20;
  bg.height = 20;
  bg.pixels.assign(20u * 20u, kMakeBgra(10, 20, 30, 255));

  SelectionResult sel{};
  sel.cancelled = false;
  sel.x = 5;
  sel.y = 5;
  sel.width = 10;
  sel.height = 10;

  std::vector<std::uint32_t> mask_px;
  renderFullscreenMask(20, 20, sel, mask_px);
  ASSERT_EQ(kPxAt(mask_px, 20, 10, 10), kClearPx);  // 选区内缘像素确认

  std::vector<std::uint32_t> out;
  ASSERT_TRUE(mask::composeBackground(bg, mask_px, out));
  // 内部像素 alpha=1：bg * 254/255 + 0 ≈ bg（1/255 舍入内）。
  const std::uint32_t expected = kMakeBgra(
      static_cast<std::uint8_t>(10u * 254u / 255u),
      static_cast<std::uint8_t>(20u * 254u / 255u),
      static_cast<std::uint8_t>(30u * 254u / 255u), 255);
  EXPECT_EQ(kPxAt(out, 20, 10, 10), expected);
  EXPECT_EQ(kPxAt(out, 20, 10, 10) >> 24, 0xFFu);
}

TEST(MaskRendererTest, ComposeBackgroundBorderIsOpaqueOrange) {
  // 边框像素（kBorderPx，premultiplied 亮橙）合成后应为纯边框色。
  Image bg;
  bg.width = 20;
  bg.height = 20;
  bg.pixels.assign(20u * 20u, kMakeBgra(255, 255, 255, 255));

  SelectionResult sel{};
  sel.cancelled = false;
  sel.x = 5;
  sel.y = 5;
  sel.width = 10;
  sel.height = 10;

  std::vector<std::uint32_t> mask_px;
  renderFullscreenMask(20, 20, sel, mask_px);
  ASSERT_EQ(kPxAt(mask_px, 20, 5, 5), kBorderPx);  // 选区顶点即边框

  std::vector<std::uint32_t> out;
  ASSERT_TRUE(mask::composeBackground(bg, mask_px, out));
  // mask alpha=0xFF：bg 权重为 0，输出=边框预乘色（亮橙 0xFFFF8000），alpha 不透明。
  EXPECT_EQ(kPxAt(out, 20, 5, 5), 0xFFFF8000u);
}

TEST(MaskRendererTest, ComposeBackgroundRejectsMismatch) {
  Image bg;
  bg.width = 2;
  bg.height = 1;
  bg.pixels.assign(2, 0xFFFFFFFFu);

  std::vector<std::uint32_t> mask_px(3, 0x59000000u);  // 尺寸不匹配
  std::vector<std::uint32_t> out(9, 0u);
  EXPECT_FALSE(mask::composeBackground(bg, mask_px, out));
  EXPECT_EQ(out.size(), 9u);  // 失败时 out 保持不变

  Image empty_bg;
  EXPECT_FALSE(mask::composeBackground(empty_bg, {}, out));
}

}  // namespace mask
}  // namespace qingying
