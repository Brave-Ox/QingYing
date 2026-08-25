#include "qingying/annotate/annotation_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace qingying {

namespace {

constexpr int MinStrokeThicknessPx = 1;

std::uint32_t packBgra(const ColorBgra& color)
{
  return (static_cast<std::uint32_t>(color.a) << 24) |
         (static_cast<std::uint32_t>(color.r) << 16) |
         (static_cast<std::uint32_t>(color.g) << 8) |
         static_cast<std::uint32_t>(color.b);
}

int toPixel(float value)
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

void drawAnnotation(Image& target, const Annotation& annotation)
{
  switch (annotation.type)
  {
    case AnnotationType::Rectangle:
      drawRectangle(target, annotation);
      break;
    default:
      // 椭圆、箭头、画笔、文字、马赛克在后续任务实现。
      break;
  }
}

}  // namespace

bool AnnotationRenderer::rasterize(const Image& source,
                                   const AnnotationDocument& document,
                                   Image& out) const
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

  return true;
}

}  // namespace qingying
