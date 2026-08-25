#include "qingying/annotate/annotation_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace qingying {

namespace {

constexpr int MinStrokeThicknessPx = 1;
constexpr double Pi = 3.14159265358979323846;
// 箭头头部：两条从终点向后张开的短线。
constexpr double ArrowHeadLengthPx = 8.0;
constexpr double ArrowHeadHalfAngleRad = Pi / 6.0;  // 30°
// 起止点几乎重合时不画头部，避免方向向量退化。
constexpr double MinArrowLengthPx = 1.0;

std::uint32_t packBgra(const ColorBgra& color)
{
  return (static_cast<std::uint32_t>(color.a) << 24) |
         (static_cast<std::uint32_t>(color.r) << 16) |
         (static_cast<std::uint32_t>(color.g) << 8) |
         static_cast<std::uint32_t>(color.b);
}

int toPixel(double value)
{
  return static_cast<int>(std::lround(value));
}

int strokeThickness(float stroke_width)
{
  const float clamped =
      std::min(std::max(stroke_width, MinStrokeWidth), MaxStrokeWidth);
  return std::max(MinStrokeThicknessPx, toPixel(clamped));
}

// 越界坐标直接丢弃，保证不会写出画布之外的像素。
void setPixel(Image& target, int x, int y, std::uint32_t value)
{
  if (x < 0 || y < 0 || x >= target.width || y >= target.height)
  {
    return;
  }

  const std::size_t index = static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(target.width) +
                            static_cast<std::size_t>(x);
  target.pixels.at(index) = value;
}

// 描边画在矩形内缘：距任意一条边不足 thickness 的像素属于边框。
// 判定使用未裁剪的边界，画布边缘不会被误判成描边。
bool isOnStroke(int x, int y, int left, int top, int right, int bottom,
                int thickness)
{
  return (x - left) < thickness || (right - x) < thickness ||
         (y - top) < thickness || (bottom - y) < thickness;
}

void drawRectangle(Image& target, const Annotation& annotation)
{
  const int left = toPixel(annotation.bounds.x);
  const int top = toPixel(annotation.bounds.y);
  const int right = left + toPixel(annotation.bounds.width) - 1;
  const int bottom = top + toPixel(annotation.bounds.height) - 1;
  if (left > right || top > bottom)
  {
    return;
  }

  const int thickness = strokeThickness(annotation.style.stroke_width);
  const std::uint32_t color = packBgra(annotation.style.color);

  const int clipped_left = std::max(0, left);
  const int clipped_top = std::max(0, top);
  const int clipped_right = std::min(target.width - 1, right);
  const int clipped_bottom = std::min(target.height - 1, bottom);

  for (int y = clipped_top; y <= clipped_bottom; ++y)
  {
    for (int x = clipped_left; x <= clipped_right; ++x)
    {
      if (isOnStroke(x, y, left, top, right, bottom, thickness))
      {
        setPixel(target, x, y, color);
      }
    }
  }
}

// 以 (x, y) 为中心落一个 thickness×thickness 的方形笔刷。
void plotBrush(Image& target, int x, int y, int thickness,
               std::uint32_t color)
{
  const int back = (thickness - 1) / 2;
  const int forward = thickness / 2;
  for (int dy = -back; dy <= forward; ++dy)
  {
    for (int dx = -back; dx <= forward; ++dx)
    {
      setPixel(target, x + dx, y + dy, color);
    }
  }
}

// Bresenham 整数直线：任意斜率都连续无空洞；越界像素由 setPixel 丢弃。
void drawLine(Image& target, int x0, int y0, int x1, int y1, int thickness,
              std::uint32_t color)
{
  const int dx = std::abs(x1 - x0);
  const int dy = -std::abs(y1 - y0);
  const int step_x = x0 < x1 ? 1 : -1;
  const int step_y = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  while (true)
  {
    plotBrush(target, x0, y0, thickness, color);
    if (x0 == x1 && y0 == y1)
    {
      break;
    }

    const int doubled = 2 * error;
    if (doubled >= dy)
    {
      error += dy;
      x0 += step_x;
    }
    if (doubled <= dx)
    {
      error += dx;
      y0 += step_y;
    }
  }
}

void drawArrow(Image& target, const Annotation& annotation)
{
  const int start_x = toPixel(annotation.start.x);
  const int start_y = toPixel(annotation.start.y);
  const int end_x = toPixel(annotation.end.x);
  const int end_y = toPixel(annotation.end.y);
  const int thickness = strokeThickness(annotation.style.stroke_width);
  const std::uint32_t color = packBgra(annotation.style.color);

  drawLine(target, start_x, start_y, end_x, end_y, thickness, color);

  const double delta_x = static_cast<double>(end_x - start_x);
  const double delta_y = static_cast<double>(end_y - start_y);
  const double length = std::sqrt(delta_x * delta_x + delta_y * delta_y);
  if (length < MinArrowLengthPx)
  {
    return;
  }

  // 从终点朝「来向」张开两条翼线。
  const double backward = std::atan2(delta_y, delta_x) + Pi;
  const double wing_signs[] = {-1.0, 1.0};
  for (const double sign : wing_signs)
  {
    const double angle = backward + sign * ArrowHeadHalfAngleRad;
    const int wing_x = end_x + toPixel(std::cos(angle) * ArrowHeadLengthPx);
    const int wing_y = end_y + toPixel(std::sin(angle) * ArrowHeadLengthPx);
    drawLine(target, end_x, end_y, wing_x, wing_y, thickness, color);
  }
}

// 画笔是开放折线：只连相邻点，首尾不闭合。
void drawPen(Image& target, const Annotation& annotation)
{
  if (annotation.points.size() < MinPenPointCount)
  {
    return;
  }

  const int thickness = strokeThickness(annotation.style.stroke_width);
  const std::uint32_t color = packBgra(annotation.style.color);

  for (std::size_t i = 1; i < annotation.points.size(); ++i)
  {
    const PointF& previous = annotation.points.at(i - 1);
    const PointF& current = annotation.points.at(i);
    drawLine(target, toPixel(previous.x), toPixel(previous.y),
             toPixel(current.x), toPixel(current.y), thickness, color);
  }
}

void drawAnnotation(Image& target, const Annotation& annotation)
{
  switch (annotation.type)
  {
    case AnnotationType::Rectangle:
      drawRectangle(target, annotation);
      break;
    case AnnotationType::Arrow:
      drawArrow(target, annotation);
      break;
    case AnnotationType::Pen:
      drawPen(target, annotation);
      break;
    default:
      // Task 7：Ellipse / Text / Mosaic 本轮空实现，不改像素。
      break;
  }
}

}  // namespace

bool AnnotationRenderer::rasterize(const Image& source,
                                   const AnnotationDocument& document,
                                   Image& out) const
{
  return rasterize(source, document, nullptr, out);
}

bool AnnotationRenderer::rasterize(const Image& source,
                                   const AnnotationDocument& document,
                                   const Annotation* preview, Image& out) const
{
  if (source.empty())
  {
    out = Image{};
    return false;
  }

  out = source;
  for (const Annotation& annotation : document.items())
  {
    drawAnnotation(out, annotation);
  }
  if (preview != nullptr)
  {
    drawAnnotation(out, *preview);
  }

  return true;
}

}  // namespace qingying
