#include "qingying/annotate/annotation_interaction_controller.hpp"

#include <algorithm>
#include <utility>

namespace qingying {

namespace {

AnnotationType toolToType(AnnotationTool tool)
{
  switch (tool)
  {
    case AnnotationTool::Rectangle:
      return AnnotationType::Rectangle;
    case AnnotationTool::Arrow:
      return AnnotationType::Arrow;
    case AnnotationTool::Pen:
      return AnnotationType::Pen;
    case AnnotationTool::Ellipse:
      return AnnotationType::Ellipse;
    case AnnotationTool::Text:
      return AnnotationType::Text;
    case AnnotationTool::Mosaic:
      return AnnotationType::Mosaic;
    case AnnotationTool::None:
    default:
      return AnnotationType::Rectangle;
  }
}

bool isDrawableTool(AnnotationTool tool)
{
  return tool == AnnotationTool::Rectangle || tool == AnnotationTool::Ellipse ||
         tool == AnnotationTool::Arrow || tool == AnnotationTool::Pen ||
         tool == AnnotationTool::Mosaic;
}

}  // namespace

void AnnotationInteractionController::setCanvasSize(int width, int height)
{
  m_canvas_width = width;
  m_canvas_height = height;
}

void AnnotationInteractionController::setTool(AnnotationTool tool)
{
  if (m_drawing)
  {
    cancelStroke();
  }
  m_tool = tool;
}

AnnotationTool AnnotationInteractionController::tool() const
{
  return m_tool;
}

bool AnnotationInteractionController::isInsideCanvas(float x, float y) const
{
  return x >= 0.0f && y >= 0.0f && x < static_cast<float>(m_canvas_width) &&
         y < static_cast<float>(m_canvas_height);
}

bool AnnotationInteractionController::beginStroke(float x, float y)
{
  if (!isDrawableTool(m_tool) || !isInsideCanvas(x, y))
  {
    return false;
  }

  m_drawing = true;
  m_start_x = x;
  m_start_y = y;
  rebuildPreview(x, y);
  return true;
}

void AnnotationInteractionController::updateStroke(float x, float y)
{
  if (!m_drawing)
  {
    return;
  }
  rebuildPreview(x, y);
}

void AnnotationInteractionController::rebuildPreview(float x, float y)
{
  Annotation annotation;
  annotation.type = toolToType(m_tool);
  annotation.style = AnnotationStyle{};

  switch (m_tool)
  {
    case AnnotationTool::Rectangle:
    case AnnotationTool::Ellipse:
    {
      const float left = (std::min)(m_start_x, x);
      const float top = (std::min)(m_start_y, y);
      const float right = (std::max)(m_start_x, x);
      const float bottom = (std::max)(m_start_y, y);
      annotation.bounds.x = left;
      annotation.bounds.y = top;
      annotation.bounds.width = right - left;
      annotation.bounds.height = bottom - top;
      break;
    }
    case AnnotationTool::Arrow:
    {
      annotation.start = PointF{m_start_x, m_start_y};
      annotation.end = PointF{x, y};
      break;
    }
    case AnnotationTool::Pen:
    case AnnotationTool::Mosaic:
    {
      const AnnotationType path_type = toolToType(m_tool);
      if (!m_has_preview || m_preview.type != path_type)
      {
        annotation.points.clear();
        annotation.points.push_back(PointF{m_start_x, m_start_y});
      }
      else
      {
        annotation.points = m_preview.points;
      }

      if (m_tool == AnnotationTool::Mosaic)
      {
        annotation.mosaic_block_size = DefaultMosaicBlockSize;
      }

      const PointF next{x, y};
      if (annotation.points.empty() ||
          annotation.points.back().x != next.x ||
          annotation.points.back().y != next.y)
      {
        // begin 时 x/y 等于起点：只保留一个点；后续移动再追加。
        if (!(annotation.points.size() == 1 &&
              annotation.points.front().x == next.x &&
              annotation.points.front().y == next.y))
        {
          annotation.points.push_back(next);
        }
      }
      break;
    }
    default:
      m_has_preview = false;
      return;
  }

  m_preview = std::move(annotation);
  m_has_preview = true;
}

bool AnnotationInteractionController::endStroke(AnnotationEngine& engine)
{
  if (!m_drawing)
  {
    return false;
  }

  const bool added = m_has_preview && engine.add(m_preview);
  m_drawing = false;
  m_has_preview = false;
  m_preview = Annotation{};
  return added;
}

void AnnotationInteractionController::cancelStroke()
{
  m_drawing = false;
  m_has_preview = false;
  m_preview = Annotation{};
}

bool AnnotationInteractionController::isDrawing() const
{
  return m_drawing;
}

bool AnnotationInteractionController::hasPreview() const
{
  return m_has_preview;
}

const Annotation& AnnotationInteractionController::preview() const
{
  return m_preview;
}

bool AnnotationInteractionController::undo(AnnotationEngine& engine)
{
  if (m_drawing)
  {
    cancelStroke();
  }
  return engine.undo();
}

}  // namespace qingying
