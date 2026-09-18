#include "qingying/annotate/annotation_renderer.hpp"
#include "qingying/annotate/color_convert.hpp"
#include "annotate/annotation_text_rasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace qingying {

namespace {

constexpr int MinStrokeThicknessPx = 1;
constexpr double Pi = 3.14159265358979323846;
constexpr double EllipseOuterPadPx = 0.5;
// 箭头头部：两条从终点向后张开的短线。
constexpr double ArrowHeadLengthPx = 8.0;
constexpr double ArrowHeadHalfAngleRad = Pi / 6.0;  // 30°
constexpr double ArrowBarHalfLengthPx = 5.0;
constexpr int CoverageSampleGrid = 4;
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
  target.pixels.at(index) =
      blendSrcOver(target.pixels.at(index), unpackColorBgra(value));
}

void setPixelCoverage(Image& target, int x, int y, std::uint32_t value,
                      double coverage)
{
  const double clamped = (std::min)((std::max)(coverage, 0.0), 1.0);
  if (clamped <= 0.0)
  {
    return;
  }
  ColorBgra color = unpackColorBgra(value);
  color.a = static_cast<std::uint8_t>(
      std::lround(static_cast<double>(color.a) * clamped));
  setPixel(target, x, y, packBgra(color));
}

// 描边画在矩形内缘：距任意一条边不足 thickness 的像素属于边框。
// 判定使用未裁剪的边界，画布边缘不会被误判成描边。
bool isOnStroke(int x, int y, int left, int top, int right, int bottom,
                int thickness)
{
  return (x - left) < thickness || (right - x) < thickness ||
         (y - top) < thickness || (bottom - y) < thickness;
}

bool strokeDashCovers(double distance, AnnotationLineStyle style);

bool strokeDashCovers(int distance, AnnotationLineStyle style)
{
  return strokeDashCovers(static_cast<double>(distance), style);
}

bool strokeDashCovers(double distance, AnnotationLineStyle style)
{
  if (style == AnnotationLineStyle::Solid)
  {
    return true;
  }

  constexpr double DashedPattern[] = {8.0, 5.0};
  constexpr double DottedPattern[] = {2.0, 4.0};
  constexpr double DashDotPattern[] = {8.0, 3.0, 2.0, 3.0};
  constexpr double DashDotDotPattern[] = {8.0, 3.0, 2.0, 3.0, 2.0, 3.0};
  const double* pattern = DashedPattern;
  int count = 2;
  switch (style)
  {
    case AnnotationLineStyle::Dotted:
      pattern = DottedPattern;
      break;
    case AnnotationLineStyle::DashDot:
      pattern = DashDotPattern;
      count = 4;
      break;
    case AnnotationLineStyle::DashDotDot:
      pattern = DashDotDotPattern;
      count = 6;
      break;
    case AnnotationLineStyle::Dashed:
    case AnnotationLineStyle::Solid:
    default:
      break;
  }

  double period = 0.0;
  for (int i = 0; i < count; ++i)
  {
    period += pattern[i];
  }
  double phase = std::fmod((std::max)(0.0, distance), period);
  for (int i = 0; i < count; ++i)
  {
    if (phase < pattern[i])
    {
      return (i % 2) == 0;
    }
    phase -= pattern[i];
  }
  return true;
}

int rectangleStrokeDistance(int x, int y, int left, int top, int right,
                            int bottom, int thickness)
{
  const int width = right - left + 1;
  const int height = bottom - top + 1;
  if ((y - top) < thickness)
  {
    return x - left;
  }
  if ((right - x) < thickness)
  {
    return width + (y - top);
  }
  if ((bottom - y) < thickness)
  {
    return width + height + (right - x);
  }
  return 2 * width + height + (bottom - y);
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
      const bool on_stroke =
          isOnStroke(x, y, left, top, right, bottom, thickness);
      if (annotation.style.filled && !on_stroke)
      {
        setPixel(target, x, y, color);
        continue;
      }
      if (!on_stroke)
      {
        continue;
      }
      const int distance = rectangleStrokeDistance(x, y, left, top, right,
                                                   bottom, thickness);
      if (strokeDashCovers(distance, annotation.style.line_style))
      {
        setPixel(target, x, y, color);
      }
    }
  }
}

double ellipseNormSq(double x, double y, double cx, double cy, double rx,
                     double ry)
{
  if (rx <= 0.0 || ry <= 0.0)
  {
    return 0.0;
  }

  const double nx = (x - cx) / rx;
  const double ny = (y - cy) / ry;
  return nx * nx + ny * ny;
}

// 描边椭圆：外椭圆为 bounds 内接椭圆；内椭圆沿半轴内缩 thickness。
// 外边界额外扩 0.5px，避免半像素圆心时整数端点落在 outer>1 之外。
bool isOnEllipseStroke(int x, int y, double cx, double cy, double rx,
                       double ry, int thickness)
{
  constexpr double HalfPixel = EllipseOuterPadPx;
  const double outer_rx = rx + HalfPixel;
  const double outer_ry = ry + HalfPixel;
  const double outer =
      ellipseNormSq(static_cast<double>(x), static_cast<double>(y), cx, cy,
                    outer_rx, outer_ry);
  if (outer > 1.0)
  {
    return false;
  }

  const double inner_rx =
      rx - static_cast<double>(thickness) - HalfPixel;
  const double inner_ry =
      ry - static_cast<double>(thickness) - HalfPixel;
  if (inner_rx <= 0.0 || inner_ry <= 0.0)
  {
    return true;
  }

  return ellipseNormSq(static_cast<double>(x), static_cast<double>(y), cx, cy,
                       inner_rx, inner_ry) > 1.0;
}

void drawEllipse(Image& target, const Annotation& annotation)
{
  const int left = toPixel(annotation.bounds.x);
  const int top = toPixel(annotation.bounds.y);
  const int width = toPixel(annotation.bounds.width);
  const int height = toPixel(annotation.bounds.height);
  if (width < MinAnnotationSizePx || height < MinAnnotationSizePx)
  {
    return;
  }

  const int right = left + width - 1;
  const int bottom = top + height - 1;
  const double cx = (static_cast<double>(left) + static_cast<double>(right)) / 2.0;
  const double cy =
      (static_cast<double>(top) + static_cast<double>(bottom)) / 2.0;
  const double rx =
      (static_cast<double>(right) - static_cast<double>(left)) / 2.0;
  const double ry =
      (static_cast<double>(bottom) - static_cast<double>(top)) / 2.0;

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
      const bool on_stroke =
          isOnEllipseStroke(x, y, cx, cy, rx, ry, thickness);
      const double outer = ellipseNormSq(static_cast<double>(x),
                                         static_cast<double>(y), cx, cy,
                                         rx + EllipseOuterPadPx,
                                         ry + EllipseOuterPadPx);
      const bool inside = outer <= 1.0;
      if (annotation.style.filled && inside && !on_stroke)
      {
        setPixel(target, x, y, color);
        continue;
      }
      if (!on_stroke)
      {
        continue;
      }
      double angle = std::atan2(static_cast<double>(y) - cy,
                                static_cast<double>(x) - cx);
      if (angle < 0.0)
      {
        angle += 2.0 * Pi;
      }
      const int distance = toPixel(angle * rx);
      if (strokeDashCovers(distance, annotation.style.line_style))
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

void drawSmoothLine(Image& target, double x0, double y0, double x1, double y1,
                    double thickness, std::uint32_t color,
                    AnnotationLineStyle style)
{
  const double delta_x = x1 - x0;
  const double delta_y = y1 - y0;
  const double length_sq = delta_x * delta_x + delta_y * delta_y;
  if (length_sq <= 0.0)
  {
    setPixelCoverage(target, toPixel(x0), toPixel(y0), color, 1.0);
    return;
  }

  const double length = std::sqrt(length_sq);
  const double radius = thickness / 2.0;
  const double pad = radius + 1.0;
  const auto draw_pixel = [&](int x, int y)
  {
    const double relative_x = static_cast<double>(x) - x0;
    const double relative_y = static_cast<double>(y) - y0;
    const double unclamped =
        (relative_x * delta_x + relative_y * delta_y) / length_sq;
    const double t = (std::min)((std::max)(unclamped, 0.0), 1.0);
    const double nearest_x = x0 + t * delta_x;
    const double nearest_y = y0 + t * delta_y;
    const double distance_x = static_cast<double>(x) - nearest_x;
    const double distance_y = static_cast<double>(y) - nearest_y;
    const double distance =
        std::sqrt(distance_x * distance_x + distance_y * distance_y);
    const double along = t * length;
    if (strokeDashCovers(along, style))
    {
      setPixelCoverage(target, x, y, color, radius + 0.5 - distance);
    }
  };

  if (std::abs(delta_x) >= std::abs(delta_y))
  {
    const int left = static_cast<int>(std::floor((std::min)(x0, x1) - pad));
    const int right = static_cast<int>(std::ceil((std::max)(x0, x1) + pad));
    for (int x = left; x <= right; ++x)
    {
      const double axis_t = std::abs(delta_x) > 0.0
                                ? (static_cast<double>(x) - x0) / delta_x
                                : 0.0;
      const double clamped_t =
          (std::min)((std::max)(axis_t, 0.0), 1.0);
      const double center_y = y0 + clamped_t * delta_y;
      const int top = static_cast<int>(std::floor(center_y - pad));
      const int bottom = static_cast<int>(std::ceil(center_y + pad));
      for (int y = top; y <= bottom; ++y)
      {
        draw_pixel(x, y);
      }
    }
    return;
  }

  const int top = static_cast<int>(std::floor((std::min)(y0, y1) - pad));
  const int bottom = static_cast<int>(std::ceil((std::max)(y0, y1) + pad));
  for (int y = top; y <= bottom; ++y)
  {
    const double axis_t = (static_cast<double>(y) - y0) / delta_y;
    const double clamped_t = (std::min)((std::max)(axis_t, 0.0), 1.0);
    const double center_x = x0 + clamped_t * delta_x;
    const int left = static_cast<int>(std::floor(center_x - pad));
    const int right = static_cast<int>(std::ceil(center_x + pad));
    for (int x = left; x <= right; ++x)
    {
      draw_pixel(x, y);
    }
  }
}

double triangleEdge(double ax, double ay, double bx, double by, double px,
                    double py)
{
  return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

bool pointInTriangle(double px, double py, const PointF& a, const PointF& b,
                     const PointF& c)
{
  const double ab = triangleEdge(a.x, a.y, b.x, b.y, px, py);
  const double bc = triangleEdge(b.x, b.y, c.x, c.y, px, py);
  const double ca = triangleEdge(c.x, c.y, a.x, a.y, px, py);
  const bool has_negative = ab < 0.0 || bc < 0.0 || ca < 0.0;
  const bool has_positive = ab > 0.0 || bc > 0.0 || ca > 0.0;
  return !(has_negative && has_positive);
}

void fillSmoothTriangle(Image& target, const PointF& a, const PointF& b,
                        const PointF& c, std::uint32_t color)
{
  const int left = static_cast<int>(
      std::floor((std::min)({a.x, b.x, c.x}) - 1.0f));
  const int top = static_cast<int>(
      std::floor((std::min)({a.y, b.y, c.y}) - 1.0f));
  const int right = static_cast<int>(
      std::ceil((std::max)({a.x, b.x, c.x}) + 1.0f));
  const int bottom = static_cast<int>(
      std::ceil((std::max)({a.y, b.y, c.y}) + 1.0f));
  constexpr double SampleOffset = 0.5 / CoverageSampleGrid;
  constexpr double SampleStep = 1.0 / CoverageSampleGrid;
  constexpr double SampleCount =
      static_cast<double>(CoverageSampleGrid * CoverageSampleGrid);

  for (int y = top; y <= bottom; ++y)
  {
    for (int x = left; x <= right; ++x)
    {
      int inside = 0;
      for (int sample_y = 0; sample_y < CoverageSampleGrid; ++sample_y)
      {
        for (int sample_x = 0; sample_x < CoverageSampleGrid; ++sample_x)
        {
          const double px = static_cast<double>(x) - 0.5 + SampleOffset +
                            sample_x * SampleStep;
          const double py = static_cast<double>(y) - 0.5 + SampleOffset +
                            sample_y * SampleStep;
          if (pointInTriangle(px, py, a, b, c))
          {
            ++inside;
          }
        }
      }
      setPixelCoverage(target, x, y, color,
                       static_cast<double>(inside) / SampleCount);
    }
  }
}

enum class ArrowMarkerKind
{
  Open,
  Filled,
  Bar,
};

void drawArrowMarker(Image& target, double tail_x, double tail_y, double tip_x,
                     double tip_y, double thickness, std::uint32_t color,
                     ArrowMarkerKind kind)
{
  const double delta_x = tip_x - tail_x;
  const double delta_y = tip_y - tail_y;
  const double length = std::sqrt(delta_x * delta_x + delta_y * delta_y);
  if (length < MinArrowLengthPx)
  {
    return;
  }
  const double unit_x = delta_x / length;
  const double unit_y = delta_y / length;
  const double normal_x = -unit_y;
  const double normal_y = unit_x;

  if (kind == ArrowMarkerKind::Bar)
  {
    const double half = ArrowBarHalfLengthPx + thickness;
    drawSmoothLine(target, tip_x - normal_x * half, tip_y - normal_y * half,
                   tip_x + normal_x * half, tip_y + normal_y * half,
                   thickness, color, AnnotationLineStyle::Solid);
    return;
  }

  const double head_length = ArrowHeadLengthPx + thickness * 1.5;
  const double half_width = head_length * std::tan(ArrowHeadHalfAngleRad);
  const PointF base_left{
      static_cast<float>(tip_x - unit_x * head_length + normal_x * half_width),
      static_cast<float>(tip_y - unit_y * head_length + normal_y * half_width)};
  const PointF base_right{
      static_cast<float>(tip_x - unit_x * head_length - normal_x * half_width),
      static_cast<float>(tip_y - unit_y * head_length - normal_y * half_width)};
  if (kind == ArrowMarkerKind::Filled)
  {
    fillSmoothTriangle(target,
                       PointF{static_cast<float>(tip_x),
                              static_cast<float>(tip_y)},
                       base_left, base_right, color);
    return;
  }

  drawSmoothLine(target, tip_x, tip_y, base_left.x, base_left.y, thickness,
                 color, AnnotationLineStyle::Solid);
  drawSmoothLine(target, tip_x, tip_y, base_right.x, base_right.y, thickness,
                 color, AnnotationLineStyle::Solid);
}

void drawArrow(Image& target, const Annotation& annotation)
{
  const double start_x = annotation.start.x;
  const double start_y = annotation.start.y;
  const double end_x = annotation.end.x;
  const double end_y = annotation.end.y;
  const double thickness =
      static_cast<double>(strokeThickness(annotation.style.stroke_width));
  const std::uint32_t color = packBgra(annotation.style.color);

  drawSmoothLine(target, start_x, start_y, end_x, end_y, thickness, color,
                 annotation.style.line_style);

  const double delta_x = end_x - start_x;
  const double delta_y = end_y - start_y;
  const double length = std::sqrt(delta_x * delta_x + delta_y * delta_y);
  if (length < MinArrowLengthPx)
  {
    return;
  }

  switch (annotation.style.arrow_style)
  {
    case AnnotationArrowStyle::EndOpen:
      drawArrowMarker(target, start_x, start_y, end_x, end_y, thickness, color,
                      ArrowMarkerKind::Open);
      break;
    case AnnotationArrowStyle::StartOpen:
      drawArrowMarker(target, end_x, end_y, start_x, start_y, thickness, color,
                      ArrowMarkerKind::Open);
      break;
    case AnnotationArrowStyle::BothOpen:
      drawArrowMarker(target, start_x, start_y, end_x, end_y, thickness, color,
                      ArrowMarkerKind::Open);
      drawArrowMarker(target, end_x, end_y, start_x, start_y, thickness, color,
                      ArrowMarkerKind::Open);
      break;
    case AnnotationArrowStyle::EndFilled:
      drawArrowMarker(target, start_x, start_y, end_x, end_y, thickness, color,
                      ArrowMarkerKind::Filled);
      break;
    case AnnotationArrowStyle::StartFilled:
      drawArrowMarker(target, end_x, end_y, start_x, start_y, thickness, color,
                      ArrowMarkerKind::Filled);
      break;
    case AnnotationArrowStyle::BothFilled:
      drawArrowMarker(target, start_x, start_y, end_x, end_y, thickness, color,
                      ArrowMarkerKind::Filled);
      drawArrowMarker(target, end_x, end_y, start_x, start_y, thickness, color,
                      ArrowMarkerKind::Filled);
      break;
    case AnnotationArrowStyle::EndBar:
      drawArrowMarker(target, start_x, start_y, end_x, end_y, thickness, color,
                      ArrowMarkerKind::Bar);
      break;
    case AnnotationArrowStyle::StartBar:
      drawArrowMarker(target, end_x, end_y, start_x, start_y, thickness, color,
                      ArrowMarkerKind::Bar);
      break;
    case AnnotationArrowStyle::BothBars:
      drawArrowMarker(target, start_x, start_y, end_x, end_y, thickness, color,
                      ArrowMarkerKind::Bar);
      drawArrowMarker(target, end_x, end_y, start_x, start_y, thickness, color,
                      ArrowMarkerKind::Bar);
      break;
    default:
      break;
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

int mosaicBlockSize(int block_size)
{
  return clampMosaicBlockSize(block_size);
}

void markBrush(std::vector<std::uint8_t>& mask, int width, int height, int x,
               int y, int thickness)
{
  const int back = (thickness - 1) / 2;
  const int forward = thickness / 2;
  for (int dy = -back; dy <= forward; ++dy)
  {
    for (int dx = -back; dx <= forward; ++dx)
    {
      const int px = x + dx;
      const int py = y + dy;
      if (px < 0 || py < 0 || px >= width || py >= height)
      {
        continue;
      }
      const std::size_t index = static_cast<std::size_t>(py) *
                                    static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(px);
      mask.at(index) = 1;
    }
  }
}

void markLine(std::vector<std::uint8_t>& mask, int width, int height, int x0,
              int y0, int x1, int y1, int thickness)
{
  const int dx = std::abs(x1 - x0);
  const int dy = -std::abs(y1 - y0);
  const int step_x = x0 < x1 ? 1 : -1;
  const int step_y = y0 < y1 ? 1 : -1;
  int error = dx + dy;

  while (true)
  {
    markBrush(mask, width, height, x0, y0, thickness);
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

// 画笔式马赛克：折线粗笔刷碰到的格子，用绘制前底图该格均值整格填回。
void drawMosaic(Image& target, const Annotation& annotation)
{
  if (annotation.points.size() < MinPenPointCount || target.empty())
  {
    return;
  }

  const int block = mosaicBlockSize(annotation.mosaic_block_size);
  const int thickness = strokeThickness(annotation.style.stroke_width);
  const int width = target.width;
  const int height = target.height;
  const std::size_t pixel_count =
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

  auto mosaic_memory = ImageMemoryBudget::global().reserve(
      static_cast<std::uint64_t>(pixel_count) * 2, ImageMemoryKind::EncodeScratch);
  if (!mosaic_memory) throw std::bad_alloc{};
  std::vector<std::uint8_t> mask(pixel_count, 0);
  for (std::size_t i = 1; i < annotation.points.size(); ++i)
  {
    const PointF& previous = annotation.points.at(i - 1);
    const PointF& current = annotation.points.at(i);
    markLine(mask, width, height, toPixel(previous.x), toPixel(previous.y),
             toPixel(current.x), toPixel(current.y), thickness);
  }

  const int tiles_x = (width + block - 1) / block;
  const int tiles_y = (height + block - 1) / block;
  std::vector<std::uint8_t> touched(
      static_cast<std::size_t>(tiles_x) * static_cast<std::size_t>(tiles_y), 0);

  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x)
    {
      const std::size_t index = static_cast<std::size_t>(y) *
                                    static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(x);
      if (mask.at(index) == 0)
      {
        continue;
      }
      const int tile_x = x / block;
      const int tile_y = y / block;
      const std::size_t tile_index = static_cast<std::size_t>(tile_y) *
                                         static_cast<std::size_t>(tiles_x) +
                                     static_cast<std::size_t>(tile_x);
      touched.at(tile_index) = 1;
    }
  }

  const Image snapshot = target;
  for (int tile_y = 0; tile_y < tiles_y; ++tile_y)
  {
    for (int tile_x = 0; tile_x < tiles_x; ++tile_x)
    {
      const std::size_t tile_index = static_cast<std::size_t>(tile_y) *
                                         static_cast<std::size_t>(tiles_x) +
                                     static_cast<std::size_t>(tile_x);
      if (touched.at(tile_index) == 0)
      {
        continue;
      }

      const int left = tile_x * block;
      const int top = tile_y * block;
      const int right = (std::min)(left + block, width);
      const int bottom = (std::min)(top + block, height);

      std::uint64_t sum_b = 0;
      std::uint64_t sum_g = 0;
      std::uint64_t sum_r = 0;
      std::uint64_t sum_a = 0;
      std::size_t count = 0;
      for (int y = top; y < bottom; ++y)
      {
        for (int x = left; x < right; ++x)
        {
          const std::size_t index = static_cast<std::size_t>(y) *
                                        static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
          const std::uint32_t px = snapshot.pixels.at(index);
          sum_b += px & 0xFFu;
          sum_g += (px >> 8) & 0xFFu;
          sum_r += (px >> 16) & 0xFFu;
          sum_a += (px >> 24) & 0xFFu;
          ++count;
        }
      }

      if (count == 0)
      {
        continue;
      }

      const std::uint32_t average =
          (static_cast<std::uint32_t>(sum_a / count) << 24) |
          (static_cast<std::uint32_t>(sum_r / count) << 16) |
          (static_cast<std::uint32_t>(sum_g / count) << 8) |
          static_cast<std::uint32_t>(sum_b / count);

      for (int y = top; y < bottom; ++y)
      {
        for (int x = left; x < right; ++x)
        {
          setPixel(target, x, y, average);
        }
      }
    }
  }
}

void drawAnnotation(Image& target, const Annotation& annotation)
{
  switch (annotation.type)
  {
    case AnnotationType::Rectangle:
      drawRectangle(target, annotation);
      break;
    case AnnotationType::Ellipse:
      drawEllipse(target, annotation);
      break;
    case AnnotationType::Arrow:
      drawArrow(target, annotation);
      break;
    case AnnotationType::Pen:
      drawPen(target, annotation);
      break;
    case AnnotationType::Text:
      rasterizeAnnotationText(target, annotation);
      break;
    case AnnotationType::Mosaic:
      drawMosaic(target, annotation);
      break;
    default:
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
