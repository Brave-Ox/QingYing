#pragma once

#include <cstddef>
#include <vector>

#include "qingying/annotate/annotation_types.hpp"

namespace qingying {

class AnnotationDocument
{
 public:
  bool add(const Annotation& annotation);
  bool undo();
  bool redo();
  void clear();

  const std::vector<Annotation>& items() const;
  bool empty() const;
  std::size_t count() const;

 private:
  bool isValid(const Annotation& annotation) const;

  std::vector<Annotation> m_items;
  std::vector<Annotation> m_redo_stack;
};

}  // namespace qingying
