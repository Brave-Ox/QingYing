#pragma once

#include <cstddef>
#include <vector>

#include "qingying/annotate/annotation_types.hpp"

namespace qingying {

class AnnotationDocument
{
 public:
  bool add(const Annotation& annotation);
  // 用合法对象替换 index 处条目；失败时文档与 redo 栈不变。
  bool replaceAt(std::size_t index, const Annotation& annotation);
  // 删除 index 处条目并压缩后续下标；越界返回 false，文档与 redo 不变。
  bool removeAt(std::size_t index);
  bool undo();
  bool redo();
  void clear();

  const std::vector<Annotation>& items() const;
  bool empty() const;
  std::size_t count() const;
  bool canUndo() const;
  bool canRedo() const;

 private:
  bool isValid(const Annotation& annotation) const;

  std::vector<Annotation> m_items;
  std::vector<Annotation> m_redo_stack;
};

}  // namespace qingying
