#pragma once

#include "qingying/action/image.hpp"
#include "qingying/geometry/rect_types.h"
#include "qingying/overlay/coordinate_transform.hpp"

namespace qingying {

// 将标注后的选区栅格贴回虚拟桌面快照，供结果操作条恢复时预览。
// 两个 Image 存储不完整或桌面尺寸不匹配时不修改 background。
bool composeCapturePreview(Image& background, const Image& selection_image,
                           const ScreenPhysicalRect& selection,
                           const coord::VirtualScreenRect& screen);

}  // namespace qingying
