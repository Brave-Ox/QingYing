#include "qingying/annotate/annotation_document.hpp"

#include <cstddef>

namespace qingying {

namespace {

bool hasMinimumSize(const RectF& bounds)
{
  const float min_size = static_cast<float>(MinAnnotationSizePx);
  return bounds.width >= min_size && bounds.height >= min_size;
}

}  // namespace

bool AnnotationDocument::isValid(const Annotation& annotation) const
{
  switch (annotation.type)
  {
    case AnnotationType::Rectangle:
    case AnnotationType::Ellipse:
      return hasMinimumSize(annotation.bounds);
    case AnnotationType::Arrow:
      return true;
    case AnnotationType::Pen:
    case AnnotationType::Mosaic:
      return annotation.points.size() >= MinPenPointCount;
    case AnnotationType::Text:
      return !annotation.text.empty();
    default:
      return false;
  }
}

bool AnnotationDocument::add(const Annotation& annotation)
{
  if (!isValid(annotation))
  {
    return false;
  }

  m_items.push_back(annotation);
  m_redo_stack.clear();
  return true;
}

bool AnnotationDocument::replaceAt(std::size_t index, const Annotation& annotation)
{
  if (index >= m_items.size() || !isValid(annotation))
  {
    return false;
  }

  m_items.at(index) = annotation;
  m_redo_stack.clear();
  return true;
}

bool AnnotationDocument::removeAt(std::size_t index)
{
  if (index >= m_items.size())
  {
    return false;
  }

  m_items.erase(m_items.begin() + static_cast<std::ptrdiff_t>(index));
  m_redo_stack.clear();
  return true;
}

bool AnnotationDocument::undo()
{
  if (m_items.empty())
  {
    return false;
  }

  m_redo_stack.push_back(m_items.back());
  m_items.pop_back();
  return true;
}

bool AnnotationDocument::redo()
{
  if (m_redo_stack.empty())
  {
    return false;
  }

  m_items.push_back(m_redo_stack.back());
  m_redo_stack.pop_back();
  return true;
}

void AnnotationDocument::clear()
{
  m_items.clear();
  m_redo_stack.clear();
}

const std::vector<Annotation>& AnnotationDocument::items() const
{
  return m_items;
}

bool AnnotationDocument::empty() const
{
  return m_items.empty();
}

std::size_t AnnotationDocument::count() const
{
  return m_items.size();
}

bool AnnotationDocument::canUndo() const
{
  return !m_items.empty();
}

bool AnnotationDocument::canRedo() const
{
  return !m_redo_stack.empty();
}

}  // namespace qingying
