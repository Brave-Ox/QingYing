#include "qingying/annotate/annotation_editor_session.hpp"

#include <utility>

namespace qingying {

bool AnnotationEditorSession::begin(const Image& source)
{
  if (m_active || source.empty())
  {
    return false;
  }

  m_engine.clear();
  m_source = source;
  m_active = true;
  return true;
}

bool AnnotationEditorSession::finishConfirmed(AnnotationFinishResult& result)
{
  if (!m_active)
  {
    return false;
  }

  Image rendered;
  if (!m_engine.render(m_source, rendered))
  {
    return false;
  }

  result.cancelled = false;
  result.rendered_image = std::move(rendered);
  m_active = false;
  return true;
}

bool AnnotationEditorSession::finishCancelled(AnnotationFinishResult& result)
{
  if (!m_active)
  {
    return false;
  }

  result.cancelled = true;
  result.rendered_image = Image{};
  m_active = false;
  return true;
}

bool AnnotationEditorSession::isActive() const
{
  return m_active;
}

const Image& AnnotationEditorSession::source() const
{
  return m_source;
}

AnnotationEngine& AnnotationEditorSession::engine()
{
  return m_engine;
}

const AnnotationEngine& AnnotationEditorSession::engine() const
{
  return m_engine;
}

}  // namespace qingying
