#include "qingying/annotate/annotation_engine.hpp"

namespace qingying {

bool AnnotationEngine::add(const Annotation& annotation)
{
  return m_document.add(annotation);
}

bool AnnotationEngine::replaceAt(std::size_t index, const Annotation& annotation)
{
  return m_document.replaceAt(index, annotation);
}

bool AnnotationEngine::removeAt(std::size_t index)
{
  return m_document.removeAt(index);
}

bool AnnotationEngine::undo()
{
  return m_document.undo();
}

bool AnnotationEngine::redo()
{
  return m_document.redo();
}

void AnnotationEngine::clear()
{
  m_document.clear();
}

bool AnnotationEngine::canUndo() const
{
  return m_document.canUndo();
}

bool AnnotationEngine::canRedo() const
{
  return m_document.canRedo();
}

bool AnnotationEngine::render(const Image& source, Image& out) const
{
  return m_renderer.rasterize(source, m_document, out);
}

const AnnotationDocument& AnnotationEngine::document() const
{
  return m_document;
}

}  // namespace qingying
