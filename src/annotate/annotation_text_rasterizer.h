#pragma once

#include "qingying/action/image.hpp"
#include "qingying/annotate/annotation_types.hpp"

namespace qingying {

// AnnotationRenderer 的平台边界：仅负责把文字字形栅格化到 BGRA32 图像。
// 几何图形和像素合成仍由 AnnotationRenderer 保持平台无关。
void rasterizeAnnotationText(Image& target, const Annotation& annotation);

}  // namespace qingying
