#pragma once

#include <algorithm>
#include <cmath>

#include "qingying/annotate/annotation_types.hpp"

namespace qingying {

// 与 ModernToolbarMetrics 默认值保持一致（避免本头文件依赖 Windows.h）。
inline constexpr int AnnotationEditorButtonWidth = 28;
inline constexpr int AnnotationEditorButtonHeight = 28;
inline constexpr int AnnotationEditorButtonGap = 4;
inline constexpr int AnnotationEditorBarPadding = 8;
inline constexpr int AnnotationEditorDividerGap = 10;
inline constexpr int AnnotationEditorDividerCount = 2;
inline constexpr int AnnotationEditorMoveButtonCount = 1;  // 左侧拖动把手
inline constexpr int AnnotationEditorToolButtonCount =
    6;  // 几何/箭头/画笔/马赛克/文字/撤销
inline constexpr int AnnotationEditorActionButtonCount = 2;  // 完成/取消
inline constexpr int AnnotationEditorChromeImageGap = 8;
inline constexpr int AnnotationEditorChromeStackGap = 6;
inline constexpr int AnnotationEditorFontComboWidth = 52;
inline constexpr int AnnotationEditorFontComboDropHeight = 200;
inline constexpr int AnnotationEditorStrokeChipIconWidth = 22;
inline constexpr int AnnotationEditorStrokeChipValueWidth = 28;
inline constexpr int AnnotationEditorStrokeChipWidth =
    AnnotationEditorStrokeChipIconWidth + AnnotationEditorStrokeChipValueWidth;
inline constexpr int AnnotationEditorStyleChipWidth = 104;
inline constexpr int AnnotationEditorStyleMenuWidth =
    AnnotationEditorStyleChipWidth;
inline constexpr int AnnotationEditorStyleMenuPadding = 4;
inline constexpr int AnnotationEditorStyleMenuItemHeight = 28;
inline constexpr int AnnotationEditorTextStyleButtonWidth = 28;
inline constexpr int AnnotationEditorFontFaceChipWidth = 200;
inline constexpr int AnnotationEditorFontMenuWidth = 240;
inline constexpr int AnnotationEditorFontMenuMaxVisibleItems = 10;
inline constexpr int AnnotationEditorStrokePopupWidth = 280;
inline constexpr int AnnotationEditorStrokePopupHeight = 72;
inline constexpr int AnnotationEditorStrokePopupCornerRadius =
    (AnnotationEditorButtonHeight + AnnotationEditorBarPadding * 2) / 2;
inline constexpr int AnnotationEditorStrokePopupPadX = 12;
inline constexpr int AnnotationEditorStrokePopupPadY = 10;
inline constexpr int AnnotationEditorStrokePopupLabelWidth = 36;
inline constexpr int AnnotationEditorStrokePopupValueWidth = 36;
inline constexpr int AnnotationEditorStrokePopupHintHeight = 22;
inline constexpr int AnnotationEditorStrokeSliderHeight = 16;
inline constexpr int AnnotationEditorStrokeSliderThumbPx = 12;
inline constexpr int AnnotationEditorStrokeSliderMinExtentPx = 1;
inline constexpr int AnnotationEditorStrokeWidthParseMaxDigits = 4;
inline constexpr float AnnotationEditorStrokeWidthRoundBias = 0.5f;
inline constexpr int AnnotationEditorFontSizeOptionCount = 4;
inline constexpr int AnnotationEditorFontSizeOptions[
    AnnotationEditorFontSizeOptionCount] = {12, 16, 24, 32};
inline constexpr int AnnotationEditorMosaicSizeOptionCount = 4;
inline constexpr int AnnotationEditorMosaicSizeOptions[
    AnnotationEditorMosaicSizeOptionCount] = {8, 12, 16, 24};
inline constexpr int AnnotationEditorColorSwatchSize = 20;
inline constexpr int AnnotationEditorCurrentColorSwatchSize =
    AnnotationEditorColorSwatchSize;
inline constexpr int AnnotationEditorColorPickerWidth = 300;
inline constexpr int AnnotationEditorColorPickerHeight = 348;
inline constexpr int AnnotationEditorColorPickerPadX = 12;
inline constexpr int AnnotationEditorColorPickerPadY = 10;
inline constexpr int AnnotationEditorColorPickerTitleHeight = 32;
inline constexpr int AnnotationEditorColorPickerCloseSize = 16;
inline constexpr int AnnotationEditorColorPickerSvHeight = 148;
inline constexpr int AnnotationEditorColorPickerSliderHeight = 12;
inline constexpr int AnnotationEditorColorPickerSliderGap = 8;
inline constexpr int AnnotationEditorColorPickerEyedropperSize = 28;
inline constexpr int AnnotationEditorColorPickerRowHeight = 26;
inline constexpr int AnnotationEditorColorPickerButtonWidth = 72;
inline constexpr int AnnotationEditorColorPickerButtonHeight = 28;
inline constexpr int AnnotationEditorColorPickerFormatWidth = 64;
inline constexpr int AnnotationEditorColorPickerHexEditWidth = 96;
inline constexpr int AnnotationEditorColorPickerChannelEditWidth = 44;
inline constexpr int AnnotationEditorColorPickerAlphaEditWidth = 48;
inline constexpr int AnnotationEditorColorPickerHueMax = 360;
inline constexpr int AnnotationEditorColorPickerPercentMax = 100;
inline constexpr int AnnotationEditorColorPickerChannelMax = 255;
inline constexpr int AnnotationEditorFrameBorderPx = 4;
inline constexpr int AnnotationEditorHandleRadiusPx = 5;
inline constexpr int AnnotationEditorFrameInsetPx = 6;
inline constexpr int AnnotationEditorSizeLabelHeightPx = 18;
inline constexpr int AnnotationEditorSizeLabelGapPx = 2;
inline constexpr int AnnotationEditorHandleCount = 8;
inline constexpr int AnnotationEditorInlineEditHeightPad = 10;
inline constexpr int AnnotationEditorInlineEditTextPadX = 8;
inline constexpr int AnnotationEditorInlineEditCaretPadPx = 8;
inline constexpr int AnnotationEditorInlineEditGlyphPadPx = 8;
inline constexpr int AnnotationEditorInlineEditBorderPx = 1;
// 空输入只留插入符与字形余量，避免 80px 白板。
inline constexpr int AnnotationEditorInlineEditMinWidth =
    AnnotationEditorInlineEditTextPadX * 2 +
    AnnotationEditorInlineEditCaretPadPx +
    AnnotationEditorInlineEditGlyphPadPx;
// PixPin 默认关闭「文本背景」：输入框不铺不透明白底。
inline constexpr bool AnnotationEditorInlineEditOpaqueFill = false;
// Win32 RGB 0x00BBGGRR，与工具栏洋红 color-key 同值，用于分层打孔。
inline constexpr std::uint32_t AnnotationEditorInlineEditColorKeyRgb =
    255u | (255u << 16);
inline constexpr int AnnotationEditorTextChromePadPx = 4;
inline constexpr int AnnotationEditorTextDeleteButtonPx = 16;
inline constexpr int AnnotationEditorTextRotationHandleRadiusPx = 9;
inline constexpr float AnnotationEditorRotationSnapDegrees = 15.0f;
inline constexpr double AnnotationEditorPi = 3.14159265358979323846;

inline constexpr int AnnotationEditorButtonCount =
    AnnotationEditorToolButtonCount + AnnotationEditorActionButtonCount;

// 主栏：拖动把手 + 工具 + 动作；字号在二级栏。
inline constexpr int AnnotationEditorToolbarControlCount =
    AnnotationEditorMoveButtonCount + AnnotationEditorButtonCount;

inline int annotationEditorToolbarHeight()
{
  return AnnotationEditorButtonHeight + AnnotationEditorBarPadding * 2;
}

inline int annotationEditorDividerExtra()
{
  return AnnotationEditorDividerCount *
         (AnnotationEditorDividerGap - AnnotationEditorButtonGap);
}

inline int annotationEditorMainToolbarWidth()
{
  return AnnotationEditorBarPadding * 2 +
         AnnotationEditorMoveButtonCount * AnnotationEditorButtonWidth +
         AnnotationEditorToolButtonCount * AnnotationEditorButtonWidth +
         AnnotationEditorActionButtonCount * AnnotationEditorButtonWidth +
         (AnnotationEditorToolbarControlCount - 1) * AnnotationEditorButtonGap +
         annotationEditorDividerExtra();
}

inline int annotationEditorButtonsWidth(int count)
{
  if (count <= 0)
  {
    return 0;
  }
  return count * AnnotationEditorButtonWidth +
         (count - 1) * AnnotationEditorButtonGap;
}

inline int annotationEditorColorSwatchesWidth()
{
  return AnnotationEditorCurrentColorSwatchSize + AnnotationEditorButtonGap +
         AnnotationStylePresetColorCount * AnnotationEditorColorSwatchSize +
         (AnnotationStylePresetColorCount - 1) * AnnotationEditorButtonGap;
}

inline int annotationEditorPropertyBarWidth()
{
  const int colors_width = annotationEditorColorSwatchesWidth();
  const int stroke_width = AnnotationEditorStrokeChipWidth;
  const int shape_width = annotationEditorButtonsWidth(2);
  const int fill_width = AnnotationEditorButtonWidth;
  const int line_style_width = AnnotationEditorStyleChipWidth;
  const int geometry_extras = shape_width + fill_width + line_style_width +
                              AnnotationEditorDividerGap * 3;
  const int after_colors =
      (std::max)(stroke_width, AnnotationEditorFontComboWidth);
  const int simple_width = AnnotationEditorBarPadding * 2 + colors_width +
                           AnnotationEditorDividerGap + after_colors;
  const int geometry_width = AnnotationEditorBarPadding * 2 + geometry_extras +
                             stroke_width + AnnotationEditorDividerGap +
                             colors_width;
  const int arrow_width = AnnotationEditorBarPadding * 2 +
                          AnnotationEditorStyleChipWidth * 2 +
                          AnnotationEditorDividerGap * 2 + stroke_width +
                          colors_width;
  const int text_width = AnnotationEditorBarPadding * 2 +
                         AnnotationEditorTextStyleButtonWidth * 2 +
                         AnnotationEditorButtonGap * 2 +
                         AnnotationEditorFontFaceChipWidth +
                         AnnotationEditorFontComboWidth +
                         AnnotationEditorDividerGap * 3 + colors_width;
  return (std::max)((std::max)((std::max)(simple_width, geometry_width),
                               arrow_width),
                    text_width);
}

// 放下主栏与二级栏所需的最小客户区宽度。
inline int annotationEditorToolbarWidth()
{
  return (std::max)(annotationEditorMainToolbarWidth(),
                    annotationEditorPropertyBarWidth());
}

inline bool annotationEditorShowsPropertyBar(AnnotationTool tool)
{
  switch (tool)
  {
    case AnnotationTool::Rectangle:
    case AnnotationTool::Ellipse:
    case AnnotationTool::Arrow:
    case AnnotationTool::Pen:
    case AnnotationTool::Text:
    case AnnotationTool::Mosaic:
      return true;
    case AnnotationTool::None:
    default:
      return false;
  }
}

inline bool annotationEditorPropertyBarShowsStroke(AnnotationTool tool)
{
  return tool == AnnotationTool::Rectangle ||
         tool == AnnotationTool::Ellipse || tool == AnnotationTool::Arrow ||
         tool == AnnotationTool::Pen;
}

inline bool annotationEditorIsGeometryTool(AnnotationTool tool)
{
  return tool == AnnotationTool::Rectangle || tool == AnnotationTool::Ellipse;
}

inline bool annotationEditorPropertyBarShowsShapeToggle(AnnotationTool tool)
{
  return annotationEditorIsGeometryTool(tool);
}

inline bool annotationEditorPropertyBarShowsFill(AnnotationTool tool)
{
  return annotationEditorIsGeometryTool(tool);
}

inline bool annotationEditorPropertyBarShowsLineStyle(AnnotationTool tool)
{
  return annotationEditorIsGeometryTool(tool) || tool == AnnotationTool::Arrow;
}

inline bool annotationEditorPropertyBarShowsArrowStyle(AnnotationTool tool)
{
  return tool == AnnotationTool::Arrow;
}

inline bool annotationEditorPropertyBarShowsFont(AnnotationTool tool)
{
  return tool == AnnotationTool::Text;
}

inline bool annotationEditorPropertyBarShowsTextStyle(AnnotationTool tool)
{
  return tool == AnnotationTool::Text;
}

inline bool annotationEditorPropertyBarShowsFontFace(AnnotationTool tool)
{
  return tool == AnnotationTool::Text;
}

inline bool annotationEditorPropertyBarShowsMosaicSize(AnnotationTool tool)
{
  return tool == AnnotationTool::Mosaic;
}

inline bool annotationEditorPropertyBarShowsSizeCombo(AnnotationTool tool)
{
  return annotationEditorPropertyBarShowsFont(tool) ||
         annotationEditorPropertyBarShowsMosaicSize(tool);
}

// 字号画在 overlay 上，不能跟描边芯片绑在一起；文字栏没有描边也必须继续画。
inline bool annotationEditorPropertyBarPaintsSizeCombo(AnnotationTool tool)
{
  return annotationEditorPropertyBarShowsSizeCombo(tool);
}

inline int annotationEditorLookupSizeOptionIndex(const int* options, int count,
                                                 int value)
{
  if (options == nullptr || count <= 0)
  {
    return -1;
  }
  for (int i = 0; i < count; ++i)
  {
    if (options[i] == value)
    {
      return i;
    }
  }
  return -1;
}

inline int annotationEditorSizeOptionAt(const int* options, int count, int index,
                                        int fallback)
{
  if (options == nullptr || index < 0 || index >= count)
  {
    return fallback;
  }
  return options[index];
}

inline int annotationEditorSizeMenuCommandToIndex(unsigned int cmd,
                                                  unsigned int base_id,
                                                  int count)
{
  if (count <= 0 || cmd < base_id)
  {
    return -1;
  }
  const unsigned int offset = cmd - base_id;
  if (offset >= static_cast<unsigned int>(count))
  {
    return -1;
  }
  return static_cast<int>(offset);
}

// 与 Win32 WHEEL_DELTA 同值，避免本头依赖 Windows.h。
inline constexpr int AnnotationEditorWheelDeltaUnit = 120;

inline int annotationEditorWheelDeltaToSteps(int delta)
{
  if (delta == 0)
  {
    return 0;
  }
  int steps = delta / AnnotationEditorWheelDeltaUnit;
  if (steps == 0)
  {
    steps = (delta > 0) ? 1 : -1;
  }
  return steps;
}

inline int annotationEditorClampFontSize(int font_size)
{
  if (font_size < MinFontSize)
  {
    return MinFontSize;
  }
  if (font_size > MaxFontSize)
  {
    return MaxFontSize;
  }
  return font_size;
}

inline int annotationEditorStepFontSize(int current, int steps)
{
  return annotationEditorClampFontSize(current + steps);
}

inline int annotationEditorStepMosaicBlockSize(int current, int steps)
{
  return clampMosaicBlockSize(current + steps);
}

// 文字工具：滚轮直接改字号（不必悬停芯片）。马赛克只在悬停块大小芯片时生效。
inline bool annotationEditorWheelAdjustsSize(AnnotationTool tool,
                                             bool hovering_size_combo)
{
  if (annotationEditorPropertyBarShowsFont(tool))
  {
    return true;
  }
  return hovering_size_combo &&
         annotationEditorPropertyBarPaintsSizeCombo(tool);
}

inline bool annotationEditorPropertyBarShowsColor(AnnotationTool tool)
{
  return annotationEditorPropertyBarShowsStroke(tool) ||
         annotationEditorPropertyBarShowsFont(tool);
}

inline int annotationEditorClampMosaicBlockSize(int block_size)
{
  return clampMosaicBlockSize(block_size);
}

inline int annotationEditorClampStrokeWidthPx(int width)
{
  const int min_width = static_cast<int>(MinStrokeWidth);
  const int max_width = static_cast<int>(MaxStrokeWidth);
  if (width < min_width)
  {
    return min_width;
  }
  if (width > max_width)
  {
    return max_width;
  }
  return width;
}

inline int annotationEditorStrokeWidthPx(float width)
{
  int value = static_cast<int>(width);
  if (width - static_cast<float>(value) >= AnnotationEditorStrokeWidthRoundBias)
  {
    ++value;
  }
  return annotationEditorClampStrokeWidthPx(value);
}

inline int annotationEditorStepStrokeWidth(int current, int steps)
{
  return annotationEditorClampStrokeWidthPx(current + steps);
}

inline bool annotationEditorParseStrokeWidthText(const wchar_t* text,
                                                 int& out_width)
{
  if (text == nullptr || text[0] == L'\0')
  {
    return false;
  }

  int value = 0;
  int digits = 0;
  for (const wchar_t* cursor = text; *cursor != L'\0'; ++cursor)
  {
    if (*cursor < L'0' || *cursor > L'9')
    {
      return false;
    }
    ++digits;
    if (digits > AnnotationEditorStrokeWidthParseMaxDigits)
    {
      return false;
    }
    value = value * 10 + static_cast<int>(*cursor - L'0');
  }
  out_width = annotationEditorClampStrokeWidthPx(value);
  return true;
}

inline int annotationEditorStrokeSliderValue(int x, int slider_left,
                                             int slider_width)
{
  const int min_width = static_cast<int>(MinStrokeWidth);
  const int max_width = static_cast<int>(MaxStrokeWidth);
  if (slider_width <= 0)
  {
    return min_width;
  }

  int pos = x - slider_left;
  if (pos < 0)
  {
    pos = 0;
  }
  if (pos > slider_width)
  {
    pos = slider_width;
  }
  const int span = max_width - min_width;
  return min_width + (pos * span + slider_width / 2) / slider_width;
}

inline int annotationEditorStrokeSliderX(int value, int slider_left,
                                         int slider_width)
{
  const int min_width = static_cast<int>(MinStrokeWidth);
  const int max_width = static_cast<int>(MaxStrokeWidth);
  const int clamped = annotationEditorClampStrokeWidthPx(value);
  const int span = max_width - min_width;
  return slider_left + (clamped - min_width) * slider_width / span;
}

inline int annotationEditorPropertyBarHeight(AnnotationTool tool)
{
  return annotationEditorShowsPropertyBar(tool)
             ? annotationEditorToolbarHeight()
             : 0;
}

inline int annotationEditorChromeHeight(AnnotationTool tool)
{
  const int main_height =
      AnnotationEditorChromeImageGap + annotationEditorToolbarHeight();
  const int property_height = annotationEditorPropertyBarHeight(tool);
  if (property_height <= 0)
  {
    return main_height;
  }
  return main_height + AnnotationEditorChromeStackGap + property_height;
}

inline int annotationEditorChromeSpanWidth(int main_width, int property_width)
{
  return (std::max)(main_width, (std::max)(0, property_width));
}

inline void annotationEditorClampRectOrigin(int& x, int& y, int width,
                                            int height, int bound_left,
                                            int bound_top, int bound_right,
                                            int bound_bottom)
{
  const int min_x = bound_left;
  int max_x = bound_right - (std::max)(1, width);
  if (max_x < min_x)
  {
    max_x = min_x;
  }
  const int min_y = bound_top;
  int max_y = bound_bottom - (std::max)(1, height);
  if (max_y < min_y)
  {
    max_y = min_y;
  }
  if (x < min_x)
  {
    x = min_x;
  }
  if (x > max_x)
  {
    x = max_x;
  }
  if (y < min_y)
  {
    y = min_y;
  }
  if (y > max_y)
  {
    y = max_y;
  }
}

// 按屏幕坐标钳制功能栏：可离开图片，但整条栏仍留在 bound 矩形内（通常是虚拟屏）。
inline void annotationEditorClampChromeOffset(int& offset_x, int& offset_y,
                                              int default_x, int default_y,
                                              int bar_width, int chrome_height,
                                              int bound_left, int bound_top,
                                              int bound_right, int bound_bottom)
{
  int x = default_x + offset_x;
  int y = default_y + offset_y;
  annotationEditorClampRectOrigin(x, y, bar_width, chrome_height, bound_left,
                                  bound_top, bound_right, bound_bottom);
  offset_x = x - default_x;
  offset_y = y - default_y;
}

inline int annotationEditorTopInset()
{
  return AnnotationEditorFrameInsetPx + AnnotationEditorSizeLabelHeightPx +
         AnnotationEditorSizeLabelGapPx;
}

inline int annotationEditorWindowWidth(int image_width)
{
  return AnnotationEditorFrameInsetPx +
         (std::max)(image_width + AnnotationEditorFrameInsetPx,
                    annotationEditorToolbarWidth());
}

// 客户区宽度至少要盖住图片、外框与整条工具栏，避免「完成/取消」被裁出窗外。
inline int annotationEditorClientWidth(int imageWidth)
{
  return annotationEditorWindowWidth(imageWidth);
}

inline int annotationEditorWindowHeight(int image_height, AnnotationTool tool)
{
  return annotationEditorTopInset() + image_height +
         annotationEditorChromeHeight(tool);
}

inline int annotationEditorInlineEditHeight(int font_size)
{
  return font_size + AnnotationEditorInlineEditHeightPad;
}

inline int annotationEditorInlineEditHeight(int font_size, int line_count)
{
  return (std::max)(1, line_count) * font_size +
         AnnotationEditorInlineEditHeightPad;
}

inline int annotationEditorInlineEditPaddedExtent(int text_extent_px)
{
  const int extent = (std::max)(0, text_extent_px);
  return extent + AnnotationEditorInlineEditTextPadX * 2 +
         AnnotationEditorInlineEditCaretPadPx +
         AnnotationEditorInlineEditGlyphPadPx;
}

inline bool annotationEditorInlineEditNeedsHScroll(int text_extent_px,
                                                   int remain_width)
{
  return annotationEditorInlineEditPaddedExtent(text_extent_px) >
         (std::max)(1, remain_width);
}

inline int annotationEditorInlineEditWidth(int text_extent_px, int remain_width)
{
  int width = (std::max)(AnnotationEditorInlineEditMinWidth,
                         annotationEditorInlineEditPaddedExtent(text_extent_px));
  width = (std::min)(width, remain_width);
  return (std::max)(1, width);
}

inline int annotationEditorTextAvailableWidth(int image_width, float text_x)
{
  const int start_x = static_cast<int>(text_x);
  return (std::max)(1, image_width - start_x);
}

inline constexpr std::size_t AnnotationEditorInvalidIndex =
    static_cast<std::size_t>(-1);

// 选中态优先；就地编辑中若选中被清掉，仍把颜色/字号打到正在编的那条。
inline std::size_t annotationEditorTextStyleTargetIndex(
    std::size_t selected_index, std::size_t editing_index, std::size_t count)
{
  if (selected_index != AnnotationEditorInvalidIndex && selected_index < count)
  {
    return selected_index;
  }
  if (editing_index != AnnotationEditorInvalidIndex && editing_index < count)
  {
    return editing_index;
  }
  return AnnotationEditorInvalidIndex;
}

enum class AnnotationEditorTextHit
{
  None,
  Body,
  Delete,
  Rotate,
};

struct AnnotationEditorRect
{
  int left{0};
  int top{0};
  int right{0};
  int bottom{0};
};

enum class AnnotationEditorStyleMenu
{
  None,
  Arrow,
  Line,
};

inline int annotationEditorFontMenuVisibleCount(int item_count)
{
  return (std::min)((std::max)(0, item_count),
                    AnnotationEditorFontMenuMaxVisibleItems);
}

inline int annotationEditorFontMenuMaxOffset(int item_count)
{
  return (std::max)(0, item_count - AnnotationEditorFontMenuMaxVisibleItems);
}

inline int annotationEditorFontMenuScrollOffset(int offset, int item_count,
                                                int steps)
{
  return (std::min)((std::max)(0, offset + steps),
                    annotationEditorFontMenuMaxOffset(item_count));
}

inline int annotationEditorFontMenuEnsureVisible(int offset, int selected_index,
                                                 int item_count)
{
  int result = annotationEditorFontMenuScrollOffset(offset, item_count, 0);
  if (selected_index < 0 || selected_index >= item_count)
  {
    return result;
  }
  if (selected_index < result)
  {
    result = selected_index;
  }
  else if (selected_index >=
           result + AnnotationEditorFontMenuMaxVisibleItems)
  {
    result = selected_index - AnnotationEditorFontMenuMaxVisibleItems + 1;
  }
  return annotationEditorFontMenuScrollOffset(result, item_count, 0);
}

inline int annotationEditorFontFaceStepIndex(int current_index, int item_count,
                                             int steps)
{
  if (item_count <= 0)
  {
    return -1;
  }
  if (current_index < 0 || current_index >= item_count)
  {
    return 0;
  }
  int next = (current_index + steps) % item_count;
  if (next < 0)
  {
    next += item_count;
  }
  return next;
}

inline AnnotationEditorRect annotationEditorFontMenuRect(
    const AnnotationEditorRect& chip, int item_count)
{
  const int visible = annotationEditorFontMenuVisibleCount(item_count);
  return AnnotationEditorRect{
      chip.left, chip.bottom + AnnotationEditorButtonGap,
      chip.left + AnnotationEditorFontMenuWidth,
      chip.bottom + AnnotationEditorButtonGap +
          AnnotationEditorStyleMenuPadding * 2 +
          visible * AnnotationEditorStyleMenuItemHeight};
}

inline AnnotationEditorRect annotationEditorFontMenuItemRect(
    const AnnotationEditorRect& menu, int visible_index)
{
  const int top = menu.top + AnnotationEditorStyleMenuPadding +
                  visible_index * AnnotationEditorStyleMenuItemHeight;
  return AnnotationEditorRect{menu.left + AnnotationEditorStyleMenuPadding,
                              top,
                              menu.right - AnnotationEditorStyleMenuPadding,
                              top + AnnotationEditorStyleMenuItemHeight};
}

inline int annotationEditorFontMenuHitTest(const AnnotationEditorRect& menu,
                                           int item_count, int scroll_offset,
                                           int x, int y)
{
  const int visible = annotationEditorFontMenuVisibleCount(item_count);
  if (x < menu.left || x >= menu.right || y < menu.top || y >= menu.bottom)
  {
    return -1;
  }
  const int relative_y = y - menu.top - AnnotationEditorStyleMenuPadding;
  if (relative_y < 0)
  {
    return -1;
  }
  const int visible_index = relative_y / AnnotationEditorStyleMenuItemHeight;
  if (visible_index >= visible)
  {
    return -1;
  }
  if (visible_index < 0)
  {
    return -1;
  }
  const int index = scroll_offset + visible_index;
  return index < item_count ? index : -1;
}

inline bool annotationEditorFontMenuConsumesEscape(bool open)
{
  return open;
}

inline AnnotationEditorRect annotationEditorStyleMenuRect(
    const AnnotationEditorRect& chip, int item_count)
{
  const int count = (std::max)(0, item_count);
  return AnnotationEditorRect{
      chip.left, chip.bottom + AnnotationEditorButtonGap,
      chip.left + AnnotationEditorStyleMenuWidth,
      chip.bottom + AnnotationEditorButtonGap +
          AnnotationEditorStyleMenuPadding * 2 +
          count * AnnotationEditorStyleMenuItemHeight};
}

inline AnnotationEditorRect annotationEditorStyleMenuItemRect(
    const AnnotationEditorRect& menu, int index)
{
  const int top = menu.top + AnnotationEditorStyleMenuPadding +
                  index * AnnotationEditorStyleMenuItemHeight;
  return AnnotationEditorRect{menu.left + AnnotationEditorStyleMenuPadding, top,
                              menu.right - AnnotationEditorStyleMenuPadding,
                              top + AnnotationEditorStyleMenuItemHeight};
}

inline bool annotationEditorPointInRect(const AnnotationEditorRect& rect, int x,
                                        int y)
{
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

inline int annotationEditorStyleMenuHitTest(const AnnotationEditorRect& menu,
                                            int item_count, int x, int y)
{
  if (!annotationEditorPointInRect(menu, x, y) || item_count <= 0)
  {
    return -1;
  }
  const int relative_y = y - menu.top - AnnotationEditorStyleMenuPadding;
  if (relative_y < 0)
  {
    return -1;
  }
  const int index = relative_y / AnnotationEditorStyleMenuItemHeight;
  return index < item_count ? index : -1;
}

inline bool annotationEditorStyleMenuConsumesEscape(
    AnnotationEditorStyleMenu menu)
{
  return menu != AnnotationEditorStyleMenu::None;
}

struct AnnotationEditorStrokePopupLayout
{
  AnnotationEditorRect label{};
  AnnotationEditorRect slider{};
  AnnotationEditorRect value{};
  AnnotationEditorRect hint{};
};

inline AnnotationEditorStrokePopupLayout annotationEditorStrokePopupLayout()
{
  AnnotationEditorStrokePopupLayout layout{};
  const int row_bottom = AnnotationEditorStrokePopupHeight -
                         AnnotationEditorStrokePopupPadY -
                         AnnotationEditorStrokePopupHintHeight;
  layout.label.left = AnnotationEditorStrokePopupPadX;
  layout.label.top = AnnotationEditorStrokePopupPadY;
  layout.label.right =
      AnnotationEditorStrokePopupPadX + AnnotationEditorStrokePopupLabelWidth;
  layout.label.bottom = row_bottom;

  layout.value.right =
      AnnotationEditorStrokePopupWidth - AnnotationEditorStrokePopupPadX;
  layout.value.left =
      layout.value.right - AnnotationEditorStrokePopupValueWidth;
  layout.value.top = AnnotationEditorStrokePopupPadY;
  layout.value.bottom = row_bottom;

  layout.slider.left = layout.label.right + AnnotationEditorButtonGap;
  layout.slider.right = layout.value.left - AnnotationEditorButtonGap;
  const int slider_pad =
      (row_bottom - AnnotationEditorStrokePopupPadY -
       AnnotationEditorStrokeSliderHeight) /
      2;
  layout.slider.top = AnnotationEditorStrokePopupPadY + slider_pad;
  layout.slider.bottom = layout.slider.top + AnnotationEditorStrokeSliderHeight;

  layout.hint.left = AnnotationEditorStrokePopupPadX;
  layout.hint.top = row_bottom;
  layout.hint.right =
      AnnotationEditorStrokePopupWidth - AnnotationEditorStrokePopupPadX;
  layout.hint.bottom =
      AnnotationEditorStrokePopupHeight - AnnotationEditorStrokePopupPadY;
  return layout;
}

// UpdateLayeredWindow 弹层不能托管可编辑 EDIT 子窗，数字框只能做独立 WS_POPUP。
inline constexpr bool AnnotationEditorStrokePopupValueIsOwnedPopup = true;

struct AnnotationEditorStrokePopupWindowState
{
  bool popup_visible{false};
  bool value_visible{false};
  int popup_screen_x{0};
  int popup_screen_y{0};
  int value_screen_x{0};
  int value_screen_y{0};
};

inline bool annotationEditorStrokePopupClosesOnTool(AnnotationTool tool)
{
  return !annotationEditorPropertyBarShowsStroke(tool);
}

inline void annotationEditorHideStrokePopupWindows(
    AnnotationEditorStrokePopupWindowState& state)
{
  state.popup_visible = false;
  state.value_visible = false;
}

inline void annotationEditorStrokePopupValueScreenOrigin(int popup_screen_x,
                                                         int popup_screen_y,
                                                         int& out_x, int& out_y)
{
  const AnnotationEditorStrokePopupLayout layout =
      annotationEditorStrokePopupLayout();
  out_x = popup_screen_x + layout.value.left;
  out_y = popup_screen_y + layout.value.top;
}

inline void annotationEditorPlaceStrokePopupWindows(
    AnnotationEditorStrokePopupWindowState& state, int popup_screen_x,
    int popup_screen_y)
{
  state.popup_visible = false;
  state.value_visible = false;
  state.popup_screen_x = popup_screen_x;
  state.popup_screen_y = popup_screen_y;
  annotationEditorStrokePopupValueScreenOrigin(
      popup_screen_x, popup_screen_y, state.value_screen_x,
      state.value_screen_y);
}

inline void annotationEditorCompleteStrokePopupPresentation(
    AnnotationEditorStrokePopupWindowState& state, bool presented)
{
  state.popup_visible = presented;
  state.value_visible =
      presented && AnnotationEditorStrokePopupValueIsOwnedPopup;
}

inline constexpr int AnnotationEditorStrokePopupValueTextMaxChars = 8;

enum class AnnotationEditorStrokePopupDeactivateTarget
{
  Outside = 0,
  Popup = 1,
  ValueEdit = 2,
};

inline bool annotationEditorStrokePopupHidesOnDeactivate(
    AnnotationEditorStrokePopupDeactivateTarget target)
{
  return target == AnnotationEditorStrokePopupDeactivateTarget::Outside;
}

inline bool annotationEditorStrokePopupAcceptsEditNotification(
    unsigned int control_id, unsigned int expected_id, bool hwnd_matches_edit)
{
  return hwnd_matches_edit || control_id == expected_id;
}

inline bool annotationEditorStrokePopupFormatWidthText(int width, wchar_t* out,
                                                       int out_chars)
{
  if (out == nullptr || out_chars < 2)
  {
    return false;
  }

  const int clamped = annotationEditorClampStrokeWidthPx(width);
  wchar_t digits[AnnotationEditorStrokeWidthParseMaxDigits]{};
  int count = 0;
  int remain = clamped;
  do
  {
    if (count >= AnnotationEditorStrokeWidthParseMaxDigits)
    {
      out[0] = L'\0';
      return false;
    }
    digits[count] = static_cast<wchar_t>(L'0' + (remain % 10));
    ++count;
    remain /= 10;
  } while (remain > 0);

  if (count >= out_chars)
  {
    out[0] = L'\0';
    return false;
  }
  for (int i = 0; i < count; ++i)
  {
    out[i] = digits[count - 1 - i];
  }
  out[count] = L'\0';
  return true;
}

struct AnnotationEditorTextChrome
{
  AnnotationEditorRect frame{};
  AnnotationEditorRect delete_button{};
  AnnotationEditorRect rotation_handle_bounds{};
  PointF corners[4]{};
  PointF center{};
  PointF delete_center{};
  PointF rotation_handle{};
  float rotation_degrees{0.0f};
};

inline bool annotationEditorContains(const AnnotationEditorRect& rect, int x,
                                     int y)
{
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

inline float annotationEditorNormalizeDegrees(float degrees)
{
  float normalized = std::fmod(degrees, 360.0f);
  if (normalized < 0.0f)
  {
    normalized += 360.0f;
  }
  if (normalized >= 360.0f)
  {
    normalized = 0.0f;
  }
  return normalized;
}

inline float annotationEditorSnapRotationDegrees(float degrees, bool snap)
{
  if (!snap)
  {
    return annotationEditorNormalizeDegrees(degrees);
  }
  const float snapped =
      std::round(degrees / AnnotationEditorRotationSnapDegrees) *
      AnnotationEditorRotationSnapDegrees;
  return annotationEditorNormalizeDegrees(snapped);
}

inline PointF annotationEditorRotatePoint(const PointF& point,
                                          const PointF& center,
                                          float degrees)
{
  const double radians =
      static_cast<double>(degrees) * AnnotationEditorPi / 180.0;
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  const double dx = static_cast<double>(point.x - center.x);
  const double dy = static_cast<double>(point.y - center.y);
  return PointF{
      center.x + static_cast<float>(dx * cosine - dy * sine),
      center.y + static_cast<float>(dx * sine + dy * cosine)};
}

inline PointF annotationEditorInverseRotatePoint(const PointF& point,
                                                 const PointF& center,
                                                 float degrees)
{
  return annotationEditorRotatePoint(point, center, -degrees);
}

enum class AnnotationEditorColorPickerHit
{
  None,
  Title,
  Close,
  Sv,
  Hue,
  Alpha,
  Eyedropper,
};

struct AnnotationEditorColorPickerLayout
{
  AnnotationEditorRect title{};
  AnnotationEditorRect close{};
  AnnotationEditorRect sv{};
  AnnotationEditorRect eyedropper{};
  AnnotationEditorRect hue{};
  AnnotationEditorRect alpha{};
  AnnotationEditorRect format{};
  AnnotationEditorRect hex_edit{};
  AnnotationEditorRect channel_edits[3]{};
  AnnotationEditorRect alpha_edit{};
  AnnotationEditorRect confirm{};
  AnnotationEditorRect cancel{};
};

inline AnnotationEditorColorPickerLayout annotationEditorColorPickerLayout()
{
  AnnotationEditorColorPickerLayout layout{};
  const int pad_x = AnnotationEditorColorPickerPadX;
  const int pad_y = AnnotationEditorColorPickerPadY;
  const int width = AnnotationEditorColorPickerWidth;
  const int height = AnnotationEditorColorPickerHeight;

  layout.title = {0, 0, width, AnnotationEditorColorPickerTitleHeight};
  layout.close = {width - pad_x - AnnotationEditorColorPickerCloseSize,
                  (AnnotationEditorColorPickerTitleHeight -
                   AnnotationEditorColorPickerCloseSize) /
                      2,
                  0, 0};
  layout.close.right = layout.close.left + AnnotationEditorColorPickerCloseSize;
  layout.close.bottom = layout.close.top + AnnotationEditorColorPickerCloseSize;

  layout.sv = {pad_x, AnnotationEditorColorPickerTitleHeight + pad_y, width - pad_x,
               AnnotationEditorColorPickerTitleHeight + pad_y +
                   AnnotationEditorColorPickerSvHeight};

  const int slider_left =
      pad_x + AnnotationEditorColorPickerEyedropperSize + AnnotationEditorButtonGap;
  int slider_top = layout.sv.bottom + pad_y;
  layout.eyedropper = {pad_x, slider_top,
                       pad_x + AnnotationEditorColorPickerEyedropperSize,
                       slider_top + AnnotationEditorColorPickerEyedropperSize};
  layout.hue = {slider_left, slider_top, width - pad_x,
                slider_top + AnnotationEditorColorPickerSliderHeight};
  slider_top += AnnotationEditorColorPickerSliderHeight +
                AnnotationEditorColorPickerSliderGap;
  layout.alpha = {slider_left, slider_top, width - pad_x,
                  slider_top + AnnotationEditorColorPickerSliderHeight};

  const int row_top = layout.eyedropper.bottom + pad_y;
  const int row_bottom = row_top + AnnotationEditorColorPickerRowHeight;
  layout.format = {pad_x, row_top, pad_x + AnnotationEditorColorPickerFormatWidth,
                   row_bottom};

  int edit_x = layout.format.right + AnnotationEditorButtonGap;
  layout.hex_edit = {edit_x, row_top,
                     edit_x + AnnotationEditorColorPickerHexEditWidth, row_bottom};
  layout.channel_edits[0] = {
      edit_x, row_top, edit_x + AnnotationEditorColorPickerChannelEditWidth,
      row_bottom};
  edit_x = layout.channel_edits[0].right + AnnotationEditorButtonGap;
  layout.channel_edits[1] = {
      edit_x, row_top, edit_x + AnnotationEditorColorPickerChannelEditWidth,
      row_bottom};
  edit_x = layout.channel_edits[1].right + AnnotationEditorButtonGap;
  layout.channel_edits[2] = {
      edit_x, row_top, edit_x + AnnotationEditorColorPickerChannelEditWidth,
      row_bottom};

  layout.alpha_edit = {width - pad_x - AnnotationEditorColorPickerAlphaEditWidth,
                       row_top, width - pad_x, row_bottom};

  layout.cancel = {width - pad_x - AnnotationEditorColorPickerButtonWidth,
                   height - pad_y - AnnotationEditorColorPickerButtonHeight,
                   width - pad_x,
                   height - pad_y};
  layout.confirm = {layout.cancel.left - AnnotationEditorButtonGap -
                        AnnotationEditorColorPickerButtonWidth,
                    layout.cancel.top,
                    layout.cancel.left - AnnotationEditorButtonGap,
                    layout.cancel.bottom};
  return layout;
}

inline AnnotationEditorColorPickerHit annotationEditorHitColorPicker(
    const AnnotationEditorColorPickerLayout& layout, int x, int y)
{
  if (annotationEditorContains(layout.close, x, y))
  {
    return AnnotationEditorColorPickerHit::Close;
  }
  if (annotationEditorContains(layout.sv, x, y))
  {
    return AnnotationEditorColorPickerHit::Sv;
  }
  if (annotationEditorContains(layout.hue, x, y))
  {
    return AnnotationEditorColorPickerHit::Hue;
  }
  if (annotationEditorContains(layout.alpha, x, y))
  {
    return AnnotationEditorColorPickerHit::Alpha;
  }
  if (annotationEditorContains(layout.eyedropper, x, y))
  {
    return AnnotationEditorColorPickerHit::Eyedropper;
  }
  if (annotationEditorContains(layout.title, x, y))
  {
    return AnnotationEditorColorPickerHit::Title;
  }
  return AnnotationEditorColorPickerHit::None;
}

inline int annotationEditorColorPickerRatio(int pos, int left, int right,
                                            int max_value)
{
  const int width = right - left;
  if (width <= 1)
  {
    return 0;
  }
  int offset = pos - left;
  if (offset < 0)
  {
    offset = 0;
  }
  if (offset > width)
  {
    offset = width;
  }
  return (offset * max_value + width / 2) / width;
}

inline void annotationEditorColorPickerSvAt(
    const AnnotationEditorRect& sv, int x, int y, int& saturation, int& value)
{
  saturation = annotationEditorColorPickerRatio(
      x, sv.left, sv.right, AnnotationEditorColorPickerPercentMax);
  value = AnnotationEditorColorPickerPercentMax -
          annotationEditorColorPickerRatio(
              y, sv.top, sv.bottom, AnnotationEditorColorPickerPercentMax);
}

inline void annotationEditorPlaceColorPicker(int anchor_x, int anchor_y,
                                             int /*anchor_w*/, int anchor_h,
                                             int bound_left, int bound_top,
                                             int bound_right, int bound_bottom,
                                             int& out_x, int& out_y)
{
  const int picker_w = AnnotationEditorColorPickerWidth;
  const int picker_h = AnnotationEditorColorPickerHeight;
  int x = anchor_x;
  int y = anchor_y - AnnotationEditorButtonGap - picker_h;
  if (y < bound_top)
  {
    y = anchor_y + (std::max)(1, anchor_h) + AnnotationEditorButtonGap;
  }
  annotationEditorClampRectOrigin(x, y, picker_w, picker_h, bound_left, bound_top,
                                  bound_right, bound_bottom);
  out_x = x;
  out_y = y;
}

inline AnnotationEditorTextChrome annotationEditorTextChrome(
    int origin_x, int origin_y, int text_x, int text_y, int text_width,
    int text_height, float rotation_degrees = 0.0f)
{
  AnnotationEditorTextChrome chrome{};
  chrome.frame.left = origin_x + text_x - AnnotationEditorTextChromePadPx;
  chrome.frame.top = origin_y + text_y - AnnotationEditorTextChromePadPx;
  chrome.frame.right =
      origin_x + text_x + text_width + AnnotationEditorTextChromePadPx;
  chrome.frame.bottom =
      origin_y + text_y + text_height + AnnotationEditorTextChromePadPx;
  chrome.delete_button.right =
      chrome.frame.right + AnnotationEditorTextDeleteButtonPx / 2;
  chrome.delete_button.left =
      chrome.delete_button.right - AnnotationEditorTextDeleteButtonPx;
  chrome.delete_button.top =
      chrome.frame.top - AnnotationEditorTextDeleteButtonPx / 2;
  chrome.delete_button.bottom =
      chrome.delete_button.top + AnnotationEditorTextDeleteButtonPx;
  chrome.center = PointF{
      static_cast<float>(chrome.frame.left + chrome.frame.right) / 2.0f,
      static_cast<float>(chrome.frame.top + chrome.frame.bottom) / 2.0f};
  const PointF local_corners[4] = {
      PointF{static_cast<float>(chrome.frame.left),
             static_cast<float>(chrome.frame.top)},
      PointF{static_cast<float>(chrome.frame.right),
             static_cast<float>(chrome.frame.top)},
      PointF{static_cast<float>(chrome.frame.right),
             static_cast<float>(chrome.frame.bottom)},
      PointF{static_cast<float>(chrome.frame.left),
             static_cast<float>(chrome.frame.bottom)}};
  chrome.rotation_degrees =
      annotationEditorNormalizeDegrees(rotation_degrees);
  for (int index = 0; index < 4; ++index)
  {
    chrome.corners[index] = annotationEditorRotatePoint(
        local_corners[index], chrome.center, chrome.rotation_degrees);
  }

  chrome.delete_center = chrome.corners[1];
  chrome.delete_button.left = static_cast<int>(std::lround(
      chrome.delete_center.x - AnnotationEditorTextDeleteButtonPx / 2.0f));
  chrome.delete_button.top = static_cast<int>(std::lround(
      chrome.delete_center.y - AnnotationEditorTextDeleteButtonPx / 2.0f));
  chrome.delete_button.right =
      chrome.delete_button.left + AnnotationEditorTextDeleteButtonPx;
  chrome.delete_button.bottom =
      chrome.delete_button.top + AnnotationEditorTextDeleteButtonPx;

  chrome.rotation_handle = chrome.corners[0];
  chrome.rotation_handle_bounds.left = static_cast<int>(std::lround(
      chrome.rotation_handle.x - AnnotationEditorTextRotationHandleRadiusPx));
  chrome.rotation_handle_bounds.top = static_cast<int>(std::lround(
      chrome.rotation_handle.y - AnnotationEditorTextRotationHandleRadiusPx));
  chrome.rotation_handle_bounds.right =
      chrome.rotation_handle_bounds.left +
      AnnotationEditorTextRotationHandleRadiusPx * 2;
  chrome.rotation_handle_bounds.bottom =
      chrome.rotation_handle_bounds.top +
      AnnotationEditorTextRotationHandleRadiusPx * 2;
  return chrome;
}

inline AnnotationEditorTextHit annotationEditorHitTextChrome(
    const AnnotationEditorTextChrome& chrome, int x, int y)
{
  if (annotationEditorContains(chrome.rotation_handle_bounds, x, y))
  {
    return AnnotationEditorTextHit::Rotate;
  }
  if (annotationEditorContains(chrome.delete_button, x, y))
  {
    return AnnotationEditorTextHit::Delete;
  }
  const PointF local = annotationEditorInverseRotatePoint(
      PointF{static_cast<float>(x), static_cast<float>(y)}, chrome.center,
      chrome.rotation_degrees);
  if (local.x >= static_cast<float>(chrome.frame.left) &&
      local.x < static_cast<float>(chrome.frame.right) &&
      local.y >= static_cast<float>(chrome.frame.top) &&
      local.y < static_cast<float>(chrome.frame.bottom))
  {
    return AnnotationEditorTextHit::Body;
  }
  return AnnotationEditorTextHit::None;
}

// 提交就地文字时 DestroyWindow 会同步触发 EN_KILLFOCUS，导致 commit 重入。
// 第二次进入必须直接放弃，否则同一条文案会被 add 两次，拖动时看起来像拖出复制。
class AnnotationEditorInlineCommitGuard
{
 public:
  explicit AnnotationEditorInlineCommitGuard(bool& busy)
      : m_busy(busy), m_owned(false)
  {
    if (!m_busy)
    {
      m_busy = true;
      m_owned = true;
    }
  }

  AnnotationEditorInlineCommitGuard(
      const AnnotationEditorInlineCommitGuard&) = delete;
  AnnotationEditorInlineCommitGuard& operator=(
      const AnnotationEditorInlineCommitGuard&) = delete;
  AnnotationEditorInlineCommitGuard(
      AnnotationEditorInlineCommitGuard&&) = delete;
  AnnotationEditorInlineCommitGuard& operator=(
      AnnotationEditorInlineCommitGuard&&) = delete;

  ~AnnotationEditorInlineCommitGuard()
  {
    if (m_owned)
    {
      m_busy = false;
    }
  }

  bool acquired() const
  {
    return m_owned;
  }

 private:
  bool& m_busy;
  bool m_owned;
};

struct AnnotationEditorHandlePoint
{
  int x{0};
  int y{0};
};

// 八点：上左、上中、上右、右中、下右、下中、下左、左中。坐标相对窗口客户区。
inline void annotationEditorHandlePoints(int origin_x, int origin_y,
                                         int image_width, int image_height,
                                         AnnotationEditorHandlePoint* out_points)
{
  if (out_points == nullptr)
  {
    return;
  }

  const int right = origin_x + image_width;
  const int bottom = origin_y + image_height;
  const int mid_x = origin_x + image_width / 2;
  const int mid_y = origin_y + image_height / 2;
  out_points[0] = {origin_x, origin_y};
  out_points[1] = {mid_x, origin_y};
  out_points[2] = {right, origin_y};
  out_points[3] = {right, mid_y};
  out_points[4] = {right, bottom};
  out_points[5] = {mid_x, bottom};
  out_points[6] = {origin_x, bottom};
  out_points[7] = {origin_x, mid_y};
}

// 就地编辑窗口摆放：图片客户区左上角必须等于选区左上角。
// 功能栏可在屏幕内任意移动；禁止为塞进屏幕而平移图片（那会造成「框选区 ≠ 编辑区」）。
struct AnnotationEditorInPlacePlacement
{
  int window_x{0};
  int window_y{0};
  int window_width{0};
  int window_height{0};
  int image_origin_x{0};
  int image_origin_y{0};
};

inline AnnotationEditorInPlacePlacement annotationEditorInPlacePlacement(
    int screen_x, int screen_y, int image_width, int image_height,
    AnnotationTool tool = AnnotationTool::None)
{
  AnnotationEditorInPlacePlacement placement{};
  placement.image_origin_x = AnnotationEditorFrameInsetPx;
  placement.image_origin_y = annotationEditorTopInset();
  placement.window_x = screen_x - placement.image_origin_x;
  placement.window_y = screen_y - placement.image_origin_y;
  placement.window_width = annotationEditorClientWidth(image_width);
  placement.window_height = annotationEditorWindowHeight(image_height, tool);
  return placement;
}

struct AnnotationEditorVirtualDesktopPlacement
{
  int window_x{0};
  int window_y{0};
  int window_width{0};
  int window_height{0};
  int image_origin_x{0};
  int image_origin_y{0};
};

// 铺满虚拟屏：图片原点只由选区屏幕坐标决定，与功能栏位置无关，避免拖栏时 SetWindowPos 造成截图框抖动。
inline AnnotationEditorVirtualDesktopPlacement
annotationEditorVirtualDesktopPlacement(int image_screen_x, int image_screen_y,
                                        int desktop_left, int desktop_top,
                                        int desktop_width, int desktop_height)
{
  AnnotationEditorVirtualDesktopPlacement placement{};
  placement.window_x = desktop_left;
  placement.window_y = desktop_top;
  placement.window_width = (std::max)(1, desktop_width);
  placement.window_height = (std::max)(1, desktop_height);
  placement.image_origin_x = image_screen_x - desktop_left;
  placement.image_origin_y = image_screen_y - desktop_top;
  return placement;
}

inline AnnotationEditorRect annotationEditorImageFrameScreenRect(
    int image_screen_x, int image_screen_y, int image_width, int image_height)
{
  AnnotationEditorRect rect{};
  rect.left = image_screen_x - AnnotationEditorFrameInsetPx;
  rect.top = image_screen_y - annotationEditorTopInset();
  rect.right = image_screen_x + image_width + AnnotationEditorFrameInsetPx;
  rect.bottom = image_screen_y + image_height + AnnotationEditorFrameInsetPx;
  return rect;
}

inline AnnotationEditorRect annotationEditorUnionRect(
    const AnnotationEditorRect& first, const AnnotationEditorRect& second)
{
  AnnotationEditorRect result{};
  result.left = (std::min)(first.left, second.left);
  result.top = (std::min)(first.top, second.top);
  result.right = (std::max)(first.right, second.right);
  result.bottom = (std::max)(first.bottom, second.bottom);
  return result;
}

struct AnnotationEditorChromeHostPlacement
{
  int window_x{0};
  int window_y{0};
  int window_width{0};
  int window_height{0};
  int image_origin_x{0};
  int image_origin_y{0};
  int chrome_client_x{0};
  int chrome_client_y{0};
};

// 宿主窗覆盖「图片外框 ∪ 功能栏」，图片屏幕坐标保持不变。
inline AnnotationEditorChromeHostPlacement annotationEditorChromeHostPlacement(
    int image_screen_x, int image_screen_y, int image_width, int image_height,
    int chrome_screen_x, int chrome_screen_y, int chrome_width,
    int chrome_height)
{
  const AnnotationEditorRect image_box = annotationEditorImageFrameScreenRect(
      image_screen_x, image_screen_y, image_width, image_height);
  AnnotationEditorRect chrome_box{};
  chrome_box.left = chrome_screen_x;
  chrome_box.top = chrome_screen_y;
  chrome_box.right = chrome_screen_x + (std::max)(1, chrome_width);
  chrome_box.bottom = chrome_screen_y + (std::max)(1, chrome_height);
  const AnnotationEditorRect host =
      annotationEditorUnionRect(image_box, chrome_box);

  AnnotationEditorChromeHostPlacement placement{};
  placement.window_x = host.left;
  placement.window_y = host.top;
  placement.window_width = (std::max)(1, host.right - host.left);
  placement.window_height = (std::max)(1, host.bottom - host.top);
  placement.image_origin_x = image_screen_x - placement.window_x;
  placement.image_origin_y = image_screen_y - placement.window_y;
  placement.chrome_client_x = chrome_screen_x - placement.window_x;
  placement.chrome_client_y = chrome_screen_y - placement.window_y;
  return placement;
}

}  // namespace qingying
