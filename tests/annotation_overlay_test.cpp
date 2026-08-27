#include "qingying/annotate/annotation_overlay.hpp"

#include <gtest/gtest.h>

#include <cstdint>

#include "qingying/annotate/annotation_document.hpp"
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
  // 冒烟图画布仅 320 宽；主栏已无字号，但仍须盖住主栏与二级栏，以免裁掉「完成/取消」。
  const int client_width = annotationEditorClientWidth(kCanvasWidth);
  EXPECT_GE(client_width, annotationEditorToolbarWidth());
  EXPECT_GE(client_width, annotationEditorPropertyBarWidth());
  EXPECT_GE(client_width, kCanvasWidth);

  const int last_control_right =
      AnnotationEditorBarPadding +
      AnnotationEditorToolButtonCount * AnnotationEditorButtonWidth +
      AnnotationEditorActionButtonCount * AnnotationEditorButtonWidth +
      (AnnotationEditorToolbarControlCount - 1) * AnnotationEditorButtonGap +
      annotationEditorDividerExtra();
  EXPECT_LE(last_control_right, client_width);
}

TEST(AnnotationOverlayTest, PropertyBarVisibilityDependsOnTool)
{
  EXPECT_TRUE(annotationEditorShowsPropertyBar(AnnotationTool::Rectangle));
  EXPECT_TRUE(annotationEditorShowsPropertyBar(AnnotationTool::Ellipse));
  EXPECT_TRUE(annotationEditorShowsPropertyBar(AnnotationTool::Arrow));
  EXPECT_TRUE(annotationEditorShowsPropertyBar(AnnotationTool::Pen));
  EXPECT_TRUE(annotationEditorShowsPropertyBar(AnnotationTool::Text));
  EXPECT_TRUE(annotationEditorShowsPropertyBar(AnnotationTool::Mosaic));
  EXPECT_FALSE(annotationEditorShowsPropertyBar(AnnotationTool::None));
}

TEST(AnnotationOverlayTest, EnteringEditorHidesPropertyBarUntilToolClick)
{
  EXPECT_FALSE(annotationEditorShowsPropertyBar(AnnotationTool::None));
  EXPECT_EQ(annotationEditorChromeHeight(AnnotationTool::None),
            annotationEditorToolbarHeight());
  EXPECT_LT(annotationEditorWindowHeight(kCanvasHeight, AnnotationTool::None),
            annotationEditorWindowHeight(kCanvasHeight,
                                         AnnotationTool::Rectangle));
}

TEST(AnnotationOverlayTest, GeometryStyleDefaultsToHollowSolidLine)
{
  const AnnotationStyle style{};
  EXPECT_FALSE(style.filled);
  EXPECT_EQ(style.line_style, AnnotationLineStyle::Solid);
}

TEST(AnnotationOverlayTest, MainToolbarMergesRectangleAndEllipse)
{
  EXPECT_EQ(AnnotationEditorToolButtonCount, 6);
}

TEST(AnnotationOverlayTest, GeometryPropertyBarShowsShapeFillAndLineStyle)
{
  EXPECT_TRUE(annotationEditorIsGeometryTool(AnnotationTool::Rectangle));
  EXPECT_TRUE(annotationEditorIsGeometryTool(AnnotationTool::Ellipse));
  EXPECT_FALSE(annotationEditorIsGeometryTool(AnnotationTool::Pen));
  EXPECT_TRUE(
      annotationEditorPropertyBarShowsShapeToggle(AnnotationTool::Rectangle));
  EXPECT_TRUE(annotationEditorPropertyBarShowsFill(AnnotationTool::Ellipse));
  EXPECT_TRUE(
      annotationEditorPropertyBarShowsLineStyle(AnnotationTool::Rectangle));
  EXPECT_FALSE(annotationEditorPropertyBarShowsShapeToggle(AnnotationTool::Pen));
  EXPECT_FALSE(annotationEditorPropertyBarShowsFill(AnnotationTool::Arrow));
  EXPECT_FALSE(annotationEditorPropertyBarShowsLineStyle(AnnotationTool::Pen));
}

TEST(AnnotationOverlayTest, StrokeToolsShowWidthNotFontOnPropertyBar)
{
  EXPECT_TRUE(annotationEditorPropertyBarShowsStroke(AnnotationTool::Rectangle));
  EXPECT_TRUE(annotationEditorPropertyBarShowsStroke(AnnotationTool::Pen));
  EXPECT_FALSE(annotationEditorPropertyBarShowsFont(AnnotationTool::Rectangle));
  EXPECT_FALSE(annotationEditorPropertyBarShowsStroke(AnnotationTool::Text));
  EXPECT_TRUE(annotationEditorPropertyBarShowsFont(AnnotationTool::Text));
  EXPECT_FALSE(annotationEditorPropertyBarShowsStroke(AnnotationTool::Mosaic));
  EXPECT_FALSE(annotationEditorPropertyBarShowsFont(AnnotationTool::Mosaic));
}

TEST(AnnotationOverlayTest, MosaicPropertyBarShowsSizeNotColorStrokeOrFont)
{
  EXPECT_TRUE(annotationEditorPropertyBarShowsMosaicSize(AnnotationTool::Mosaic));
  EXPECT_FALSE(annotationEditorPropertyBarShowsMosaicSize(AnnotationTool::Pen));
  EXPECT_FALSE(annotationEditorPropertyBarShowsMosaicSize(AnnotationTool::Text));
  EXPECT_FALSE(annotationEditorPropertyBarShowsColor(AnnotationTool::Mosaic));
  EXPECT_TRUE(annotationEditorPropertyBarShowsColor(AnnotationTool::Pen));
  EXPECT_TRUE(annotationEditorPropertyBarShowsColor(AnnotationTool::Text));
}

TEST(AnnotationOverlayTest, SizeComboShownForTextAndMosaicOnly)
{
  EXPECT_TRUE(annotationEditorPropertyBarShowsSizeCombo(AnnotationTool::Text));
  EXPECT_TRUE(annotationEditorPropertyBarShowsSizeCombo(AnnotationTool::Mosaic));
  EXPECT_FALSE(annotationEditorPropertyBarShowsSizeCombo(AnnotationTool::Pen));
  EXPECT_FALSE(annotationEditorPropertyBarShowsSizeCombo(AnnotationTool::Rectangle));
}

TEST(AnnotationOverlayTest, MosaicSizeComboPresetsIncludeDefaultTwelve)
{
  EXPECT_EQ(AnnotationEditorMosaicSizeOptionCount, 4);
  EXPECT_EQ(AnnotationEditorMosaicSizeOptions[0], 8);
  EXPECT_EQ(AnnotationEditorMosaicSizeOptions[1], 12);
  EXPECT_EQ(AnnotationEditorMosaicSizeOptions[2], 16);
  EXPECT_EQ(AnnotationEditorMosaicSizeOptions[3], 24);
  EXPECT_EQ(AnnotationEditorMosaicSizeOptions[1], DefaultMosaicBlockSize);
}

TEST(AnnotationOverlayTest, ChromeHeightAddsPropertyBarForMosaic)
{
  EXPECT_EQ(annotationEditorChromeHeight(AnnotationTool::Rectangle),
            annotationEditorToolbarHeight() * 2);
  EXPECT_EQ(annotationEditorChromeHeight(AnnotationTool::Text),
            annotationEditorToolbarHeight() * 2);
  EXPECT_EQ(annotationEditorChromeHeight(AnnotationTool::Mosaic),
            annotationEditorToolbarHeight() * 2);
}

TEST(AnnotationOverlayTest, ClampMosaicBlockSizeKeepsDefaultAndClampsRange)
{
  EXPECT_EQ(annotationEditorClampMosaicBlockSize(DefaultMosaicBlockSize),
            DefaultMosaicBlockSize);
  EXPECT_EQ(annotationEditorClampMosaicBlockSize(0), 1);
  EXPECT_EQ(annotationEditorClampMosaicBlockSize(-4), 1);
  EXPECT_EQ(annotationEditorClampMosaicBlockSize(999), 32);
}

TEST(AnnotationOverlayTest, InPlacePlacementPinsImageOriginToSelection)
{
  constexpr int kSelX = 100;
  constexpr int kSelY = 200;
  constexpr int kImgW = 80;
  constexpr int kImgH = 60;

  const AnnotationEditorInPlacePlacement place =
      annotationEditorInPlacePlacement(kSelX, kSelY, kImgW, kImgH);

  EXPECT_EQ(place.window_x + place.image_origin_x, kSelX);
  EXPECT_EQ(place.window_y + place.image_origin_y, kSelY);
  EXPECT_EQ(place.image_origin_x, AnnotationEditorFrameInsetPx);
  EXPECT_EQ(place.image_origin_y, annotationEditorTopInset());
  EXPECT_EQ(place.window_width, annotationEditorWindowWidth(kImgW));
  EXPECT_EQ(place.window_height,
            annotationEditorWindowHeight(kImgH, AnnotationTool::None));
}

TEST(AnnotationOverlayTest, InPlacePlacementDoesNotShiftToFitToolbarOnScreen)
{
  // 贴近屏幕底部的选区：旧逻辑会为塞下工具栏而上移整个窗口，导致图片错位。
  constexpr int kSelX = 10;
  constexpr int kSelY = 1000;
  constexpr int kImgW = 400;
  constexpr int kImgH = 200;

  const AnnotationEditorInPlacePlacement place =
      annotationEditorInPlacePlacement(kSelX, kSelY, kImgW, kImgH);

  EXPECT_EQ(place.window_x + place.image_origin_x, kSelX);
  EXPECT_EQ(place.window_y + place.image_origin_y, kSelY);
  EXPECT_GT(place.window_height, kImgH);
}

TEST(AnnotationOverlayTest, WindowExpandsOutwardFromImageForFrame)
{
  constexpr int kImgW = 80;
  constexpr int kImgH = 60;
  EXPECT_GE(annotationEditorWindowWidth(kImgW),
            kImgW + AnnotationEditorFrameInsetPx * 2);
  EXPECT_EQ(annotationEditorWindowHeight(kImgH, AnnotationTool::Rectangle),
            annotationEditorTopInset() + kImgH +
                annotationEditorChromeHeight(AnnotationTool::Rectangle));
  EXPECT_EQ(annotationEditorTopInset(),
            AnnotationEditorFrameInsetPx + AnnotationEditorSizeLabelHeightPx +
                AnnotationEditorSizeLabelGapPx);
}

TEST(AnnotationOverlayTest, AnnotationColorToRgbMatchesWin32Order)
{
  // 黄：B=0,G=220,R=255 → RGB(255,220,0)，输入框 CTLCOLOR 必须用这个顺序。
  const ColorBgra yellow = AnnotationStylePresetColors[2];
  EXPECT_EQ(annotationColorToRgb(yellow), 255u | (220u << 8));
  const ColorBgra white = AnnotationStylePresetColors[7];
  EXPECT_EQ(annotationColorToRgb(white), 255u | (255u << 8) | (255u << 16));
}

TEST(AnnotationOverlayTest, InlineEditHeightTracksFontSize)
{
  EXPECT_EQ(annotationEditorInlineEditHeight(12),
            12 + AnnotationEditorInlineEditHeightPad);
  EXPECT_EQ(annotationEditorInlineEditHeight(32),
            32 + AnnotationEditorInlineEditHeightPad);
}

TEST(AnnotationOverlayTest, InlineEditWidthGrowsWithTextThenClamps)
{
  EXPECT_EQ(annotationEditorInlineEditWidth(10, 400),
            AnnotationEditorInlineEditMinWidth);
  const int mid =
      120 + AnnotationEditorInlineEditTextPadX * 2;
  EXPECT_EQ(annotationEditorInlineEditWidth(120, 400), mid);
  EXPECT_EQ(annotationEditorInlineEditWidth(400, 400),
            AnnotationEditorInlineEditMaxWidth);
  EXPECT_EQ(annotationEditorInlineEditWidth(120, 50), 50);
  EXPECT_EQ(annotationEditorInlineEditWidth(120, 0), 1);
}

TEST(AnnotationOverlayTest, TextChromePutsDeleteButtonOnTopRight)
{
  constexpr int kOriginX = 6;
  constexpr int kOriginY = 26;
  constexpr int kTextX = 40;
  constexpr int kTextY = 20;
  constexpr int kTextW = 80;
  constexpr int kTextH = 32;
  const AnnotationEditorTextChrome chrome = annotationEditorTextChrome(
      kOriginX, kOriginY, kTextX, kTextY, kTextW, kTextH);

  EXPECT_EQ(chrome.frame.left, kOriginX + kTextX - AnnotationEditorTextChromePadPx);
  EXPECT_EQ(chrome.frame.top, kOriginY + kTextY - AnnotationEditorTextChromePadPx);
  EXPECT_EQ(chrome.frame.right,
            kOriginX + kTextX + kTextW + AnnotationEditorTextChromePadPx);
  EXPECT_EQ(chrome.frame.bottom,
            kOriginY + kTextY + kTextH + AnnotationEditorTextChromePadPx);
  EXPECT_EQ(chrome.delete_button.right,
            chrome.frame.right + AnnotationEditorTextDeleteButtonPx / 2);
  EXPECT_EQ(chrome.delete_button.top,
            chrome.frame.top - AnnotationEditorTextDeleteButtonPx / 2);
  EXPECT_EQ(chrome.delete_button.right - chrome.delete_button.left,
            AnnotationEditorTextDeleteButtonPx);
  EXPECT_EQ(chrome.delete_button.bottom - chrome.delete_button.top,
            AnnotationEditorTextDeleteButtonPx);
}

TEST(AnnotationOverlayTest, TextChromeHitTestPrefersDeleteOverBody)
{
  constexpr int kOriginX = 0;
  constexpr int kOriginY = 0;
  const AnnotationEditorTextChrome chrome =
      annotationEditorTextChrome(kOriginX, kOriginY, 10, 10, 40, 20);
  const int delete_x =
      (chrome.delete_button.left + chrome.delete_button.right) / 2;
  const int delete_y =
      (chrome.delete_button.top + chrome.delete_button.bottom) / 2;
  EXPECT_EQ(annotationEditorHitTextChrome(chrome, delete_x, delete_y),
            AnnotationEditorTextHit::Delete);
  EXPECT_EQ(annotationEditorHitTextChrome(chrome, chrome.frame.left + 2,
                                          chrome.frame.top + 2),
            AnnotationEditorTextHit::Body);
  EXPECT_EQ(annotationEditorHitTextChrome(chrome, 0, 0),
            AnnotationEditorTextHit::None);
}

TEST(AnnotationOverlayTest, InlineCommitGuardRejectsReentrantAcquire)
{
  bool busy = false;
  const AnnotationEditorInlineCommitGuard outer(busy);
  ASSERT_TRUE(outer.acquired());

  const AnnotationEditorInlineCommitGuard inner(busy);
  EXPECT_FALSE(inner.acquired());
}

TEST(AnnotationOverlayTest, InlineCommitGuardAllowsAcquireAfterLeave)
{
  bool busy = false;
  {
    const AnnotationEditorInlineCommitGuard first(busy);
    ASSERT_TRUE(first.acquired());
  }
  const AnnotationEditorInlineCommitGuard second(busy);
  EXPECT_TRUE(second.acquired());
}

TEST(AnnotationOverlayTest, ReentrantTextCommitAddsOnlyOnce)
{
  AnnotationDocument document;
  bool busy = false;
  Annotation text;
  text.type = AnnotationType::Text;
  text.text = L"demo";
  text.start.x = 10.0f;
  text.start.y = 20.0f;

  const auto commit = [&document, &busy, &text](auto&& self) -> void
  {
    const AnnotationEditorInlineCommitGuard guard(busy);
    if (!guard.acquired())
    {
      return;
    }
    self(self);
    ASSERT_TRUE(document.add(text));
  };
  commit(commit);

  EXPECT_EQ(document.count(), 1u);
  EXPECT_EQ(document.items().at(0).text, L"demo");
}

TEST(AnnotationOverlayTest, HandlePointsSitOnImageEdges)
{
  const int kOriginX = AnnotationEditorFrameInsetPx;
  const int kOriginY = annotationEditorTopInset();
  constexpr int kImgW = 80;
  constexpr int kImgH = 60;
  AnnotationEditorHandlePoint points[AnnotationEditorHandleCount]{};
  annotationEditorHandlePoints(kOriginX, kOriginY, kImgW, kImgH, points);

  EXPECT_EQ(points[0].x, kOriginX);
  EXPECT_EQ(points[0].y, kOriginY);
  EXPECT_EQ(points[1].x, kOriginX + kImgW / 2);
  EXPECT_EQ(points[1].y, kOriginY);
  EXPECT_EQ(points[2].x, kOriginX + kImgW);
  EXPECT_EQ(points[2].y, kOriginY);
  EXPECT_EQ(points[3].x, kOriginX + kImgW);
  EXPECT_EQ(points[3].y, kOriginY + kImgH / 2);
  EXPECT_EQ(points[4].x, kOriginX + kImgW);
  EXPECT_EQ(points[4].y, kOriginY + kImgH);
  EXPECT_EQ(points[5].x, kOriginX + kImgW / 2);
  EXPECT_EQ(points[5].y, kOriginY + kImgH);
  EXPECT_EQ(points[6].x, kOriginX);
  EXPECT_EQ(points[6].y, kOriginY + kImgH);
  EXPECT_EQ(points[7].x, kOriginX);
  EXPECT_EQ(points[7].y, kOriginY + kImgH / 2);
}

// 手工冒烟：会真实弹出编辑器窗口，需要人工操作，因此默认跳过。
// 运行：
//   qingying_tests.exe --gtest_also_run_disabled_tests `
//                      --gtest_filter=AnnotationOverlayTest.DISABLED_*
//
// 预期：
// 1. 窗口居中，蓝白横条纹图（最上一道蓝色）
// 2. 底部主栏：几何(矩形/椭圆)/箭头/画笔/马赛克/文字 | 撤销 | 完成/取消
//    点几何后二级栏：形状切换+填充+线型+线宽+色块；文字为色块+字号；
//    马赛克为块大小下拉（8/12/16/24，默认 12），无色块
// 3. 默认矩形：拖出框有预览，松开后保留；Ctrl+Z 或点撤销可去掉
// 4. 文字：空白单击新建；已有文字单击出现黑框+删除；拖过阈值可改位置；
//    双击进入就地编辑（输入中显示所选颜色，透明底细黑框）；二级栏可改颜色/字号
// 5. 切换箭头、画笔同样可画，二级栏改色/线宽对下一笔生效；点完成得到合成图；Esc/取消不改结果语义
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
