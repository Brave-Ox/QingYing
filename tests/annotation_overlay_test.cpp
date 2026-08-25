#include "qingying/annotate/annotation_overlay.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "qingying/annotate/annotation_editor_layout.hpp"

namespace qingying {
namespace {

constexpr int kCanvasWidth = 320;
constexpr int kCanvasHeight = 200;
constexpr std::uint32_t kWhitePx = 0xFFFFFFFFu;
constexpr std::uint32_t kBluePx = 0xFF3060C0u;
constexpr int kStripeHeight = 20;

// 带横向条纹的测试图，便于手工冒烟时肉眼确认图像方向没有上下颠倒。
Image makeStripedCanvas()
{
  Image image;
  image.width = kCanvasWidth;
  image.height = kCanvasHeight;
  image.pixels.assign(static_cast<std::size_t>(kCanvasWidth) *
                          static_cast<std::size_t>(kCanvasHeight),
                      kWhitePx);
  for (int y = 0; y < kCanvasHeight; ++y)
  {
    if ((y / kStripeHeight) % 2 != 0)
    {
      continue;
    }
    for (int x = 0; x < kCanvasWidth; ++x)
    {
      image.pixels.at(static_cast<std::size_t>(y) *
                          static_cast<std::size_t>(kCanvasWidth) +
                      static_cast<std::size_t>(x)) = kBluePx;
    }
  }
  return image;
}

}  // namespace

TEST(AnnotationOverlayTest, NewOverlayIsNotVisible)
{
  const AnnotationOverlay overlay;
  EXPECT_FALSE(overlay.isVisible());
}

TEST(AnnotationOverlayTest, ShowRejectsEmptySourceWithoutInvokingCallback)
{
  AnnotationOverlay overlay;
  const Image empty;
  bool invoked = false;

  const bool shown = overlay.show(nullptr, empty,
                                  [&invoked](const AnnotationFinishResult&)
                                  {
                                    invoked = true;
                                  });

  EXPECT_FALSE(shown);
  EXPECT_FALSE(invoked);
  EXPECT_FALSE(overlay.isVisible());
}

TEST(AnnotationOverlayTest, HideOnIdleOverlayIsSafe)
{
  AnnotationOverlay overlay;
  overlay.hide();
  EXPECT_FALSE(overlay.isVisible());
}

TEST(AnnotationOverlayTest, ClientWidthFitsAllToolbarButtonsForNarrowImage)
{
  // 冒烟图画布仅 320 宽；工具栏含字号下拉，必须扩宽以免裁掉「完成/取消」。
  const int client_width = annotationEditorClientWidth(kCanvasWidth);
  EXPECT_GE(client_width, annotationEditorToolbarWidth());
  EXPECT_GE(client_width, kCanvasWidth);

  const int last_control_right =
      AnnotationEditorBarPadding +
      AnnotationEditorToolButtonCount * AnnotationEditorButtonWidth +
      AnnotationEditorFontComboWidth +
      AnnotationEditorActionButtonCount * AnnotationEditorButtonWidth +
      (AnnotationEditorToolbarControlCount - 1) * AnnotationEditorButtonGap +
      annotationEditorDividerExtra();
  EXPECT_LE(last_control_right, client_width);
}

// 手工冒烟：会真实弹出编辑器窗口，需要人工操作，因此默认跳过。
// 运行：
//   qingying_tests.exe --gtest_also_run_disabled_tests `
//                      --gtest_filter=AnnotationOverlayTest.DISABLED_*
//
// 预期：
// 1. 窗口居中，蓝白横条纹图（最上一道蓝色）
// 2. 底部现代图标工具条：矩形/椭圆/箭头/画笔/文字/字号/撤销 + 完成/取消
// 3. 默认矩形：拖出框有预览，松开后保留；Ctrl+Z 或点撤销可去掉
// 4. 文字：空白单击新建；已有文字单击选中（虚线框）后 Delete 删除；
//    双击进入就地编辑；拖过阈值可改位置
// 5. 切换箭头、画笔同样可画；点完成得到合成图；Esc/取消不改结果语义
// 6. 框选后操作条同为圆角白底图标条（复制/下载/编辑/钉图）
TEST(AnnotationOverlayTest, DISABLED_SmokeConfirmReturnsSourceCopy)
{
  AnnotationOverlay overlay;
  const Image source = makeStripedCanvas();
  AnnotationFinishResult result;

  ASSERT_TRUE(overlay.show(nullptr, source,
                           [&result](const AnnotationFinishResult& finished)
                           {
                             result = finished;
                           }));

  EXPECT_FALSE(overlay.isVisible());
  if (!result.cancelled)
  {
    EXPECT_EQ(result.rendered_image.width, kCanvasWidth);
    EXPECT_EQ(result.rendered_image.height, kCanvasHeight);
    // 若画过标注，像素应与源图不同；若直接完成，则等于源图拷贝。
    EXPECT_FALSE(result.rendered_image.empty());
  }
  else
  {
    EXPECT_TRUE(result.rendered_image.empty());
  }
}

}  // namespace qingying
