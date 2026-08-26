#pragma once

#include <algorithm>

#include "qingying/annotate/annotation_types.hpp"

namespace qingying {

// 与 ModernToolbarMetrics 默认值保持一致（避免本头文件依赖 Windows.h）。
inline constexpr int AnnotationEditorButtonWidth = 28;
inline constexpr int AnnotationEditorButtonHeight = 28;
inline constexpr int AnnotationEditorButtonGap = 4;
inline constexpr int AnnotationEditorBarPadding = 8;
inline constexpr int AnnotationEditorDividerGap = 10;
inline constexpr int AnnotationEditorDividerCount = 2;
inline constexpr int AnnotationEditorToolButtonCount =
    7;  // 矩形/椭圆/箭头/画笔/马赛克/文字/撤销
inline constexpr int AnnotationEditorActionButtonCount = 2;  // 完成/取消
inline constexpr int AnnotationEditorFontComboWidth = 52;
inline constexpr int AnnotationEditorFontComboDropHeight = 200;
inline constexpr int AnnotationEditorFontSizeOptionCount = 4;
inline constexpr int AnnotationEditorFontSizeOptions[
    AnnotationEditorFontSizeOptionCount] = {12, 16, 24, 32};
inline constexpr int AnnotationEditorColorSwatchSize = 20;
inline constexpr int AnnotationEditorFrameBorderPx = 4;
inline constexpr int AnnotationEditorHandleRadiusPx = 5;
inline constexpr int AnnotationEditorFrameInsetPx = 6;
inline constexpr int AnnotationEditorSizeLabelHeightPx = 18;
inline constexpr int AnnotationEditorSizeLabelGapPx = 2;
inline constexpr int AnnotationEditorHandleCount = 8;
inline constexpr int AnnotationEditorInlineEditMinWidth = 80;
inline constexpr int AnnotationEditorInlineEditMaxWidth = 220;
inline constexpr int AnnotationEditorInlineEditHeightPad = 10;
inline constexpr int AnnotationEditorInlineEditTextPadX = 8;
inline constexpr int AnnotationEditorInlineEditBorderPx = 1;
inline constexpr int AnnotationEditorTextChromePadPx = 4;
inline constexpr int AnnotationEditorTextDeleteButtonPx = 16;

inline constexpr int AnnotationEditorButtonCount =
    AnnotationEditorToolButtonCount + AnnotationEditorActionButtonCount;

// 主栏只放工具与动作，字号在二级栏。
inline constexpr int AnnotationEditorToolbarControlCount =
    AnnotationEditorButtonCount;

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
         AnnotationEditorToolButtonCount * AnnotationEditorButtonWidth +
         AnnotationEditorActionButtonCount * AnnotationEditorButtonWidth +
         (AnnotationEditorToolbarControlCount - 1) * AnnotationEditorButtonGap +
         annotationEditorDividerExtra();
}

inline int annotationEditorPropertyBarWidth()
{
  const int colors_width =
      AnnotationStylePresetColorCount * AnnotationEditorColorSwatchSize +
      (AnnotationStylePresetColorCount - 1) * AnnotationEditorButtonGap;
  const int stroke_width =
      AnnotationStylePresetStrokeCount * AnnotationEditorButtonWidth +
      (AnnotationStylePresetStrokeCount - 1) * AnnotationEditorButtonGap;
  const int after_colors =
      (std::max)(stroke_width, AnnotationEditorFontComboWidth);
  return AnnotationEditorBarPadding * 2 + colors_width +
         AnnotationEditorDividerGap + after_colors;
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
      return true;
    case AnnotationTool::Mosaic:
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

inline bool annotationEditorPropertyBarShowsFont(AnnotationTool tool)
{
  return tool == AnnotationTool::Text;
}

inline int annotationEditorPropertyBarHeight(AnnotationTool tool)
{
  return annotationEditorShowsPropertyBar(tool)
             ? annotationEditorToolbarHeight()
             : 0;
}

inline int annotationEditorChromeHeight(AnnotationTool tool)
{
  return annotationEditorToolbarHeight() +
         annotationEditorPropertyBarHeight(tool);
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

inline int annotationEditorInlineEditWidth(int text_extent_px, int remain_width)
{
  const int padded =
      text_extent_px + AnnotationEditorInlineEditTextPadX * 2;
  int width = (std::max)(AnnotationEditorInlineEditMinWidth, padded);
  width = (std::min)(width, AnnotationEditorInlineEditMaxWidth);
  width = (std::min)(width, remain_width);
  return (std::max)(1, width);
}

enum class AnnotationEditorTextHit
{
  None,
  Body,
  Delete,
};

struct AnnotationEditorRect
{
  int left{0};
  int top{0};
  int right{0};
  int bottom{0};
};

struct AnnotationEditorTextChrome
{
  AnnotationEditorRect frame{};
  AnnotationEditorRect delete_button{};
};

inline bool annotationEditorContains(const AnnotationEditorRect& rect, int x,
                                     int y)
{
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

inline AnnotationEditorTextChrome annotationEditorTextChrome(
    int origin_x, int origin_y, int text_x, int text_y, int text_width,
    int text_height)
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
  return chrome;
}

inline AnnotationEditorTextHit annotationEditorHitTextChrome(
    const AnnotationEditorTextChrome& chrome, int x, int y)
{
  if (annotationEditorContains(chrome.delete_button, x, y))
  {
    return AnnotationEditorTextHit::Delete;
  }
  if (annotationEditorContains(chrome.frame, x, y))
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
// 工具栏可伸出屏幕外；禁止为塞进屏幕而平移图片（那会造成「框选区 ≠ 编辑区」）。
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
    AnnotationTool tool = AnnotationTool::Rectangle)
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

}  // namespace qingying
