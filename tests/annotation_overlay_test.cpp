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
      AnnotationEditorMoveButtonCount * AnnotationEditorButtonWidth +
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
            AnnotationEditorChromeImageGap + annotationEditorToolbarHeight());
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
  EXPECT_EQ(AnnotationEditorMoveButtonCount, 1);
  EXPECT_EQ(AnnotationEditorToolButtonCount, 6);
  EXPECT_EQ(AnnotationEditorToolbarControlCount,
            AnnotationEditorMoveButtonCount + AnnotationEditorButtonCount);
}

TEST(AnnotationOverlayTest, CompactMainBarIsNarrowerThanWideImage)
{
  EXPECT_LT(annotationEditorMainToolbarWidth(), 800);
  EXPECT_GE(annotationEditorClientWidth(800), 800);
}

TEST(AnnotationOverlayTest, ChromeOffsetClampsToScreenBoundsNotImageBox)
{
  constexpr int kDefaultX = 100;
  constexpr int kDefaultY = 200;
  constexpr int kBarW = 120;
  constexpr int kBarH = 50;
  constexpr int kScreenLeft = 0;
  constexpr int kScreenTop = 0;
  constexpr int kScreenRight = 1920;
  constexpr int kScreenBottom = 1080;

  int offset_x = 0;
  int offset_y = 0;
  annotationEditorClampChromeOffset(offset_x, offset_y, kDefaultX, kDefaultY,
                                    kBarW, kBarH, kScreenLeft, kScreenTop,
                                    kScreenRight, kScreenBottom);
  EXPECT_EQ(offset_x, 0);
  EXPECT_EQ(offset_y, 0);

  // 远离图片默认位置，但仍在屏幕内：不得再钳回图片窗口。
  offset_x = 800;
  offset_y = 400;
  annotationEditorClampChromeOffset(offset_x, offset_y, kDefaultX, kDefaultY,
                                    kBarW, kBarH, kScreenLeft, kScreenTop,
                                    kScreenRight, kScreenBottom);
  EXPECT_EQ(kDefaultX + offset_x, 900);
  EXPECT_EQ(kDefaultY + offset_y, 600);

  offset_x = 5000;
  offset_y = 5000;
  annotationEditorClampChromeOffset(offset_x, offset_y, kDefaultX, kDefaultY,
                                    kBarW, kBarH, kScreenLeft, kScreenTop,
                                    kScreenRight, kScreenBottom);
  EXPECT_EQ(kDefaultX + offset_x, kScreenRight - kBarW);
  EXPECT_EQ(kDefaultY + offset_y, kScreenBottom - kBarH);

  offset_x = -800;
  offset_y = -400;
  annotationEditorClampChromeOffset(offset_x, offset_y, kDefaultX, kDefaultY,
                                    kBarW, kBarH, kScreenLeft, kScreenTop,
                                    kScreenRight, kScreenBottom);
  EXPECT_EQ(kDefaultX + offset_x, kScreenLeft);
  EXPECT_EQ(kDefaultY + offset_y, kScreenTop);
}

TEST(AnnotationOverlayTest, ChromeHostKeepsImagePinnedWhenChromeMovesAway)
{
  constexpr int kSelX = 100;
  constexpr int kSelY = 200;
  constexpr int kImgW = 80;
  constexpr int kImgH = 60;
  constexpr int kChromeX = 800;
  constexpr int kChromeY = 40;
  constexpr int kChromeW = 240;
  constexpr int kChromeH = 50;

  const AnnotationEditorChromeHostPlacement host =
      annotationEditorChromeHostPlacement(kSelX, kSelY, kImgW, kImgH, kChromeX,
                                          kChromeY, kChromeW, kChromeH);

  EXPECT_EQ(host.window_x + host.image_origin_x, kSelX);
  EXPECT_EQ(host.window_y + host.image_origin_y, kSelY);
  EXPECT_EQ(host.window_x + host.chrome_client_x, kChromeX);
  EXPECT_EQ(host.window_y + host.chrome_client_y, kChromeY);
  EXPECT_LE(host.window_x, kChromeX);
  EXPECT_LE(host.window_y, kChromeY);
  EXPECT_GE(host.window_x + host.window_width, kChromeX + kChromeW);
  EXPECT_GE(host.window_y + host.window_height, kChromeY + kChromeH);
}

TEST(AnnotationOverlayTest, ChromeHostAtDefaultMatchesCompactEditor)
{
  constexpr int kSelX = 100;
  constexpr int kSelY = 200;
  constexpr int kImgW = 80;
  constexpr int kImgH = 60;
  const int chrome_x = kSelX;
  const int chrome_y =
      kSelY + kImgH + AnnotationEditorChromeImageGap;
  const int chrome_w = annotationEditorMainToolbarWidth();
  const int chrome_h = annotationEditorToolbarHeight();

  const AnnotationEditorChromeHostPlacement host =
      annotationEditorChromeHostPlacement(kSelX, kSelY, kImgW, kImgH, chrome_x,
                                          chrome_y, chrome_w, chrome_h);
  const AnnotationEditorInPlacePlacement place =
      annotationEditorInPlacePlacement(kSelX, kSelY, kImgW, kImgH);

  EXPECT_EQ(host.window_x + host.image_origin_x, kSelX);
  EXPECT_EQ(host.window_y + host.image_origin_y, kSelY);
  EXPECT_EQ(host.window_x, place.window_x);
  EXPECT_EQ(host.window_y, place.window_y);
  EXPECT_EQ(host.window_height, place.window_height);
}

TEST(AnnotationOverlayTest, ChromeHostShiftsClientOriginWhenChromeCrossesImageEdge)
{
  // 包围盒方案会在功能栏越过图片左/上沿时改 window 原点，客户区 image_origin 跟着变。
  // 这正是边缘处截图框抖动的来源：SetWindowPos 与重绘不同步。
  constexpr int kSelX = 100;
  constexpr int kSelY = 200;
  constexpr int kImgW = 80;
  constexpr int kImgH = 60;
  const AnnotationEditorChromeHostPlacement below =
      annotationEditorChromeHostPlacement(kSelX, kSelY, kImgW, kImgH, kSelX,
                                          kSelY + kImgH + AnnotationEditorChromeImageGap, 240, 50);
  const AnnotationEditorChromeHostPlacement left =
      annotationEditorChromeHostPlacement(kSelX, kSelY, kImgW, kImgH, 0, kSelY,
                                          240, 50);
  EXPECT_NE(below.image_origin_x, left.image_origin_x);
  EXPECT_EQ(below.window_x + below.image_origin_x, kSelX);
  EXPECT_EQ(left.window_x + left.image_origin_x, kSelX);
}

TEST(AnnotationOverlayTest, VirtualDesktopPlacementKeepsImageOriginStable)
{
  constexpr int kSelX = 400;
  constexpr int kSelY = 300;
  constexpr int kDeskLeft = -1920;
  constexpr int kDeskTop = 0;
  constexpr int kDeskW = 3840;
  constexpr int kDeskH = 1080;

  const AnnotationEditorVirtualDesktopPlacement place =
      annotationEditorVirtualDesktopPlacement(kSelX, kSelY, kDeskLeft, kDeskTop,
                                              kDeskW, kDeskH);

  EXPECT_EQ(place.window_x, kDeskLeft);
  EXPECT_EQ(place.window_y, kDeskTop);
  EXPECT_EQ(place.window_width, kDeskW);
  EXPECT_EQ(place.window_height, kDeskH);
  EXPECT_EQ(place.window_x + place.image_origin_x, kSelX);
  EXPECT_EQ(place.window_y + place.image_origin_y, kSelY);
  EXPECT_EQ(place.image_origin_x, kSelX - kDeskLeft);
  EXPECT_EQ(place.image_origin_y, kSelY - kDeskTop);
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

TEST(AnnotationOverlayTest, TextSizeComboStillPaintsWhenStrokeHidden)
{
  // 文字栏没有描边芯片；若 paint 在「无描边」处直接 return，字号芯片永远不会画到 overlay。
  EXPECT_FALSE(annotationEditorPropertyBarShowsStroke(AnnotationTool::Text));
  EXPECT_TRUE(annotationEditorPropertyBarPaintsSizeCombo(AnnotationTool::Text));
  EXPECT_TRUE(annotationEditorPropertyBarPaintsSizeCombo(AnnotationTool::Mosaic));
  EXPECT_FALSE(annotationEditorPropertyBarPaintsSizeCombo(AnnotationTool::Pen));
}

TEST(AnnotationOverlayTest, FontSizeOptionLookupMapsPresetTable)
{
  EXPECT_EQ(annotationEditorLookupSizeOptionIndex(
                AnnotationEditorFontSizeOptions,
                AnnotationEditorFontSizeOptionCount, 12),
            0);
  EXPECT_EQ(annotationEditorLookupSizeOptionIndex(
                AnnotationEditorFontSizeOptions,
                AnnotationEditorFontSizeOptionCount, 24),
            2);
  EXPECT_EQ(annotationEditorLookupSizeOptionIndex(
                AnnotationEditorFontSizeOptions,
                AnnotationEditorFontSizeOptionCount, 99),
            -1);
  EXPECT_EQ(annotationEditorSizeOptionAt(AnnotationEditorFontSizeOptions,
                                         AnnotationEditorFontSizeOptionCount, 2,
                                         DefaultFontSize),
            24);
  EXPECT_EQ(annotationEditorSizeOptionAt(AnnotationEditorFontSizeOptions,
                                         AnnotationEditorFontSizeOptionCount, -1,
                                         DefaultFontSize),
            DefaultFontSize);
}

TEST(AnnotationOverlayTest, SizeMenuCommandMapsBackToOptionIndex)
{
  constexpr UINT kBase = 400;
  EXPECT_EQ(annotationEditorSizeMenuCommandToIndex(kBase, kBase,
                                                   AnnotationEditorFontSizeOptionCount),
            0);
  EXPECT_EQ(annotationEditorSizeMenuCommandToIndex(
                kBase + 3, kBase, AnnotationEditorFontSizeOptionCount),
            3);
  EXPECT_EQ(annotationEditorSizeMenuCommandToIndex(
                kBase + 4, kBase, AnnotationEditorFontSizeOptionCount),
            -1);
  EXPECT_EQ(annotationEditorSizeMenuCommandToIndex(
                kBase - 1, kBase, AnnotationEditorFontSizeOptionCount),
            -1);
}

TEST(AnnotationOverlayTest, WheelDeltaConvertsToAtLeastOneStep)
{
  EXPECT_EQ(annotationEditorWheelDeltaToSteps(AnnotationEditorWheelDeltaUnit),
            1);
  EXPECT_EQ(annotationEditorWheelDeltaToSteps(-AnnotationEditorWheelDeltaUnit),
            -1);
  EXPECT_EQ(annotationEditorWheelDeltaToSteps(AnnotationEditorWheelDeltaUnit * 2),
            2);
  EXPECT_EQ(annotationEditorWheelDeltaToSteps(
                AnnotationEditorWheelDeltaUnit / 2),
            1);
  EXPECT_EQ(annotationEditorWheelDeltaToSteps(-1), -1);
}

TEST(AnnotationOverlayTest, StepFontSizeStaysInRangeLikeStroke)
{
  EXPECT_EQ(annotationEditorStepFontSize(DefaultFontSize, 1),
            DefaultFontSize + 1);
  EXPECT_EQ(annotationEditorStepFontSize(MinFontSize, -1), MinFontSize);
  EXPECT_EQ(annotationEditorStepFontSize(MaxFontSize, 1), MaxFontSize);
  EXPECT_EQ(annotationEditorStepMosaicBlockSize(DefaultMosaicBlockSize, 1),
            DefaultMosaicBlockSize + 1);
  EXPECT_EQ(annotationEditorStepMosaicBlockSize(MinMosaicBlockSize, -1),
            MinMosaicBlockSize);
}

TEST(AnnotationOverlayTest, TextToolWheelAdjustsSizeWithoutStrokeChip)
{
  // 文字栏没有描边芯片；滚轮必须走字号，而不是被 handleStrokeChipWheel 直接丢掉。
  EXPECT_FALSE(annotationEditorPropertyBarShowsStroke(AnnotationTool::Text));
  EXPECT_TRUE(annotationEditorWheelAdjustsSize(AnnotationTool::Text, false));
  EXPECT_TRUE(annotationEditorWheelAdjustsSize(AnnotationTool::Text, true));
  EXPECT_TRUE(annotationEditorWheelAdjustsSize(AnnotationTool::Mosaic, true));
  EXPECT_FALSE(annotationEditorWheelAdjustsSize(AnnotationTool::Mosaic, false));
  EXPECT_FALSE(annotationEditorWheelAdjustsSize(AnnotationTool::Pen, false));
  EXPECT_FALSE(annotationEditorWheelAdjustsSize(AnnotationTool::Pen, true));
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
  const int expected =
      AnnotationEditorChromeImageGap + annotationEditorToolbarHeight() +
      AnnotationEditorChromeStackGap + annotationEditorToolbarHeight();
  EXPECT_EQ(annotationEditorChromeHeight(AnnotationTool::Rectangle), expected);
  EXPECT_EQ(annotationEditorChromeHeight(AnnotationTool::Text), expected);
  EXPECT_EQ(annotationEditorChromeHeight(AnnotationTool::Mosaic), expected);
}

TEST(AnnotationOverlayTest, IdleChromeIsSinglePillBelowImage)
{
  EXPECT_EQ(annotationEditorChromeHeight(AnnotationTool::None),
            AnnotationEditorChromeImageGap + annotationEditorToolbarHeight());
}

TEST(AnnotationOverlayTest, ClampMosaicBlockSizeKeepsDefaultAndClampsRange)
{
  EXPECT_EQ(annotationEditorClampMosaicBlockSize(DefaultMosaicBlockSize),
            DefaultMosaicBlockSize);
  EXPECT_EQ(annotationEditorClampMosaicBlockSize(0), 1);
  EXPECT_EQ(annotationEditorClampMosaicBlockSize(-4), 1);
  EXPECT_EQ(annotationEditorClampMosaicBlockSize(999), 32);
}

TEST(AnnotationOverlayTest, StrokeChipUsesPixPinValueControlWidth)
{
  EXPECT_EQ(AnnotationEditorStrokeChipWidth,
            AnnotationEditorStrokeChipIconWidth +
                AnnotationEditorStrokeChipValueWidth);
  EXPECT_LT(AnnotationEditorStrokeChipWidth,
            annotationEditorButtonsWidth(AnnotationStylePresetStrokeCount));
}

TEST(AnnotationOverlayTest, ClampAndStepStrokeWidthStayInRange)
{
  EXPECT_EQ(annotationEditorClampStrokeWidthPx(0),
            static_cast<int>(MinStrokeWidth));
  EXPECT_EQ(annotationEditorClampStrokeWidthPx(999),
            static_cast<int>(MaxStrokeWidth));
  EXPECT_EQ(annotationEditorStrokeWidthPx(DefaultStrokeWidth), 3);
  EXPECT_EQ(annotationEditorStepStrokeWidth(3, 1), 4);
  EXPECT_EQ(annotationEditorStepStrokeWidth(1, -1),
            static_cast<int>(MinStrokeWidth));
  EXPECT_EQ(annotationEditorStepStrokeWidth(
                static_cast<int>(MaxStrokeWidth), 1),
            static_cast<int>(MaxStrokeWidth));
}

TEST(AnnotationOverlayTest, ParseStrokeWidthTextRejectsNonDigitsAndClamps)
{
  int width = 0;
  EXPECT_TRUE(annotationEditorParseStrokeWidthText(L"8", width));
  EXPECT_EQ(width, 8);
  EXPECT_TRUE(annotationEditorParseStrokeWidthText(L"99", width));
  EXPECT_EQ(width, static_cast<int>(MaxStrokeWidth));
  EXPECT_FALSE(annotationEditorParseStrokeWidthText(L"", width));
  EXPECT_FALSE(annotationEditorParseStrokeWidthText(L"12a", width));
}

TEST(AnnotationOverlayTest, StrokeSliderMapsEndsAndMidpoint)
{
  EXPECT_EQ(annotationEditorStrokeSliderValue(0, 0, 150),
            static_cast<int>(MinStrokeWidth));
  EXPECT_EQ(annotationEditorStrokeSliderValue(150, 0, 150),
            static_cast<int>(MaxStrokeWidth));
  const AnnotationEditorStrokePopupLayout layout =
      annotationEditorStrokePopupLayout();
  EXPECT_GT(layout.slider.right, layout.slider.left);
  EXPECT_EQ(layout.value.right - layout.value.left,
            AnnotationEditorStrokePopupValueWidth);
}

TEST(AnnotationOverlayTest, StrokePopupCornerMatchesChromeStadium)
{
  EXPECT_EQ(AnnotationEditorStrokePopupCornerRadius,
            annotationEditorToolbarHeight() / 2);
  EXPECT_GT(AnnotationEditorStrokePopupCornerRadius, 8);
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

TEST(AnnotationOverlayTest, TextStyleTargetUsesSelectionOrEditingIndex)
{
  constexpr std::size_t kInvalid = AnnotationEditorInvalidIndex;
  EXPECT_EQ(annotationEditorTextStyleTargetIndex(1, 0, 3), 1u);
  EXPECT_EQ(annotationEditorTextStyleTargetIndex(kInvalid, 2, 3), 2u);
  EXPECT_EQ(annotationEditorTextStyleTargetIndex(kInvalid, kInvalid, 3),
            kInvalid);
  EXPECT_EQ(annotationEditorTextStyleTargetIndex(5, 1, 3), 1u);
  EXPECT_EQ(annotationEditorTextStyleTargetIndex(0, 2, 0), kInvalid);
}

TEST(AnnotationOverlayTest, TextAnnotationUsesPreMergeMicrosoftYaHeiUi)
{
  EXPECT_STREQ(AnnotationTextFontFace, L"Microsoft YaHei UI");
}

TEST(AnnotationOverlayTest, InlineEditMinWidthHugsCaretInsteadOfWhiteSlab)
{
  // PixPin 默认无白底填充；空输入只留插入符宽度，不再铺 80px 白板。
  EXPECT_LT(AnnotationEditorInlineEditMinWidth, 80);
  EXPECT_EQ(AnnotationEditorInlineEditMinWidth,
            annotationEditorInlineEditPaddedExtent(0));
  EXPECT_FALSE(AnnotationEditorInlineEditOpaqueFill);
  EXPECT_NE(AnnotationEditorInlineEditColorKeyRgb, 0x00FFFFFFu);
}

TEST(AnnotationOverlayTest, InlineEditWidthGrowsWithTextThenClampsToRemain)
{
  EXPECT_EQ(annotationEditorInlineEditWidth(0, 400),
            AnnotationEditorInlineEditMinWidth);
  EXPECT_EQ(annotationEditorInlineEditWidth(10, 400),
            annotationEditorInlineEditPaddedExtent(10));
  const int mid = 120 + AnnotationEditorInlineEditTextPadX * 2 +
                  AnnotationEditorInlineEditCaretPadPx +
                  AnnotationEditorInlineEditGlyphPadPx;
  EXPECT_EQ(annotationEditorInlineEditWidth(120, 400), mid);
  // 旧实现硬限制 220px，长文案输入时会被裁掉；宽度应随文字涨到剩余空间。
  const int long_text = 300 + AnnotationEditorInlineEditTextPadX * 2 +
                        AnnotationEditorInlineEditCaretPadPx +
                        AnnotationEditorInlineEditGlyphPadPx;
  EXPECT_GT(long_text, 220);
  EXPECT_EQ(annotationEditorInlineEditWidth(300, 800), long_text);
  EXPECT_EQ(annotationEditorInlineEditWidth(400, 400), 400);
  EXPECT_EQ(annotationEditorInlineEditWidth(120, 50), 50);
  EXPECT_EQ(annotationEditorInlineEditWidth(120, 0), 1);
}

TEST(AnnotationOverlayTest, InlineEditFormatWidthLeavesGlyphAndCaret)
{
  // EM_SETMARGINS 会吃掉左右 TextPadX；扣完后必须仍能放下正文、插入符和字形左伸出，
  // 否则 ES_AUTOHSCROLL 会把首字滚出视口（输入中 'w' 被切、结束后 DrawText 又正常）。
  const int extent = 200;
  const int width = annotationEditorInlineEditWidth(extent, 1000);
  const int inner = width - AnnotationEditorInlineEditTextPadX * 2;
  EXPECT_GE(inner, extent + AnnotationEditorInlineEditCaretPadPx +
                       AnnotationEditorInlineEditGlyphPadPx);
  EXPECT_FALSE(annotationEditorInlineEditNeedsHScroll(extent, 1000));
  EXPECT_TRUE(annotationEditorInlineEditNeedsHScroll(800, 100));
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
// 6. 框选后操作条同为圆角白底图标条（复制/下载 | 长截图/编辑/钉图/停止）
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
