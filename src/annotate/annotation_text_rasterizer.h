#pragma once

#include "annotate/gdi_raii.hpp"
#include "qingying/action/image.hpp"
#include "qingying/annotate/annotation_types.hpp"

namespace qingying {

GdiObject createAnnotationTextFont(const AnnotationStyle& style,
                                   int font_size);

// AnnotationRenderer 的平台边界：仅负责把文字字形栅格化到 BGRA32 图像。
// 几何图形和像素合成仍由 AnnotationRenderer 保持平台无关。
void rasterizeAnnotationText(Image& target, const Annotation& annotation);

}  // namespace qingying
