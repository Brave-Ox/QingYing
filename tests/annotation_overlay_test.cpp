#include "qingying/annotate/annotation_overlay.hpp"

#include <gtest/gtest.h>

#include <cstdint>

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

// 手工冒烟：会真实弹出编辑器窗口，需要人工点击，因此默认跳过。
// 运行方式见 docs 或提交说明：
//   qingying_tests.exe --gtest_also_run_disabled_tests \
//                      --gtest_filter=AnnotationOverlayTest.DISABLED_*
//
// 预期：窗口居中弹出，显示蓝白横条纹图（第一条为蓝色，说明未上下颠倒），
// 底部有「完成」「取消」两个按钮。
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

  testing::Message() << "点「完成」应得到 cancelled=false 且图像与源图一致；"
                        "点「取消」/按 Esc/关闭窗口应得到 cancelled=true。";
  EXPECT_FALSE(overlay.isVisible());
  if (!result.cancelled)
  {
    EXPECT_EQ(result.rendered_image.width, kCanvasWidth);
    EXPECT_EQ(result.rendered_image.height, kCanvasHeight);
    EXPECT_EQ(result.rendered_image.pixels, source.pixels);
  }
  else
  {
    EXPECT_TRUE(result.rendered_image.empty());
  }
}

}  // namespace qingying
