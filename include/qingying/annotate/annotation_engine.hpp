#pragma once

#include "qingying/action/image.hpp"
#include "qingying/annotate/annotation_document.hpp"
#include "qingying/annotate/annotation_renderer.hpp"

namespace qingying {

// 标注引擎：把文档模型与栅格化组装成编辑器的唯一入口。
// 只做数据与像素，不处理 Win32 消息，也不调用 ActionDispatcher /
// ExportService / CaptureEngine。
class AnnotationEngine
{
 public:
  // 几何非法的对象会被拒绝并返回 false，且不影响撤销栈。
  bool add(const Annotation& annotation);
  bool replaceAt(std::size_t index, const Annotation& annotation);
  bool removeAt(std::size_t index);
  bool undo();
  bool redo();
  void clear();

  bool canUndo() const;
  bool canRedo() const;

  // 源图只读；成功时 out 为「源图 + 标注」的新图（BGRA32，同尺寸）。
  // 源图为空时返回 false 并把 out 置空。
  bool render(const Image& source, Image& out) const;

  const AnnotationDocument& document() const;

 private:
  AnnotationDocument m_document;
  AnnotationRenderer m_renderer;
};

}  // namespace qingying
