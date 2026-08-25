#pragma once

#include "qingying/action/image.hpp"
#include "qingying/annotate/annotation_document.hpp"

namespace qingying {

// 把标注文档栅格化到源图的副本上：源图保持只读，结果写入 out。
// 纯计算，无 Win32 依赖，可单元测试。
class AnnotationRenderer
{
 public:
  // 成功时 out 为「源图 + 标注」的新图（BGRA32，尺寸与源图一致）。
  // 源图为空时返回 false，并把 out 置空。
  bool rasterize(const Image& source, const AnnotationDocument& document,
                 Image& out) const;

  // 在已提交文档之上额外绘制 preview（拖拽预览，不要求通过文档校验）。
  // preview 为 nullptr 时与三参数版本等价。
  bool rasterize(const Image& source, const AnnotationDocument& document,
                 const Annotation* preview, Image& out) const;
};

}  // namespace qingying
