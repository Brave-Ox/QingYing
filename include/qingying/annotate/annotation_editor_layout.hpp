#pragma once

#include <algorithm>

namespace qingying {

// 标注编辑器底部工具栏布局常量（与 Overlay 创建按钮保持一致）。
inline constexpr int AnnotationEditorButtonWidth = 72;
inline constexpr int AnnotationEditorButtonHeight = 30;
inline constexpr int AnnotationEditorButtonGap = 6;
inline constexpr int AnnotationEditorBarPadding = 6;
inline constexpr int AnnotationEditorToolButtonCount =
    6;  // 矩形/椭圆/箭头/画笔/文字/撤销
inline constexpr int AnnotationEditorActionButtonCount = 2;  // 完成/取消
inline constexpr int AnnotationEditorFontComboWidth = 72;
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

// 放下全部控件所需的最小客户区宽度。
inline int annotationEditorToolbarWidth()
{
  return AnnotationEditorBarPadding * 2 +
         AnnotationEditorToolButtonCount * AnnotationEditorButtonWidth +
         AnnotationEditorFontComboWidth +
         AnnotationEditorActionButtonCount * AnnotationEditorButtonWidth +
         (AnnotationEditorToolbarControlCount - 1) * AnnotationEditorButtonGap;
}

// 客户区宽度至少要盖住图片与整条工具栏，避免「完成/取消」被裁出窗外。
inline int annotationEditorClientWidth(int imageWidth)
{
  return (std::max)(imageWidth, annotationEditorToolbarWidth());
}

}  // namespace qingying
