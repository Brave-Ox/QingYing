#pragma once

#include <algorithm>

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

inline constexpr int AnnotationEditorButtonCount =
    AnnotationEditorToolButtonCount + AnnotationEditorActionButtonCount;

// 工具按钮 + 字号下拉 + 完成/取消。
inline constexpr int AnnotationEditorToolbarControlCount =
    AnnotationEditorButtonCount + 1;

inline int annotationEditorToolbarHeight()
{
  return AnnotationEditorButtonHeight + AnnotationEditorBarPadding * 2;
}

inline int annotationEditorDividerExtra()
{
  return AnnotationEditorDividerCount *
         (AnnotationEditorDividerGap - AnnotationEditorButtonGap);
}

// 放下全部控件所需的最小客户区宽度。
inline int annotationEditorToolbarWidth()
{
  return AnnotationEditorBarPadding * 2 +
         AnnotationEditorToolButtonCount * AnnotationEditorButtonWidth +
         AnnotationEditorFontComboWidth +
         AnnotationEditorActionButtonCount * AnnotationEditorButtonWidth +
         (AnnotationEditorToolbarControlCount - 1) * AnnotationEditorButtonGap +
         annotationEditorDividerExtra();
}

// 客户区宽度至少要盖住图片与整条工具栏，避免「完成/取消」被裁出窗外。
inline int annotationEditorClientWidth(int imageWidth)
{
  return (std::max)(imageWidth, annotationEditorToolbarWidth());
}

// 就地编辑窗口摆放：图片客户区左上角必须等于选区左上角。
// 工具栏可伸出屏幕外；禁止为塞进屏幕而平移图片（那会造成「框选区 ≠ 编辑区」）。
struct AnnotationEditorInPlacePlacement
{
  int window_x{0};
  int window_y{0};
  int window_width{0};
  int window_height{0};
};

inline AnnotationEditorInPlacePlacement annotationEditorInPlacePlacement(
    int screen_x, int screen_y, int image_width, int image_height)
{
  AnnotationEditorInPlacePlacement placement{};
  placement.window_x = screen_x;
  placement.window_y = screen_y;
  placement.window_width = annotationEditorClientWidth(image_width);
  placement.window_height =
      image_height + annotationEditorToolbarHeight();
  return placement;
}

}  // namespace qingying
