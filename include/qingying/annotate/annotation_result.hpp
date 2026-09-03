#pragma once

#include <functional>

#include "qingying/action/image.hpp"

namespace qingying {

// 标注编辑器的收尾结果。第三人只需消费这两个字段：
// cancelled 为 true 时忽略图像并保持 ResultStore 当前结果不变；
// 为 false 时 rendered_image 是与源图同尺寸的 BGRA32 合成图。
struct AnnotationFinishResult
{
  bool cancelled{true};
  Image rendered_image;
};

using AnnotationCallback = std::function<void(const AnnotationFinishResult&)>;

}  // namespace qingying
