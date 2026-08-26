#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qingying {

enum class AnnotationType
{
  Rectangle,
  Ellipse,
  Arrow,
  Pen,
  Text,
  Mosaic,  // 画笔式：points 折线 + mosaic_block_size
};

enum class AnnotationTool
{
  None,
  Rectangle,
  Ellipse,
  Arrow,
  Pen,
  Text,
  Mosaic,
};

inline constexpr float DefaultStrokeWidth = 3.0f;
inline constexpr float MinStrokeWidth = 1.0f;
inline constexpr float MaxStrokeWidth = 16.0f;
inline constexpr float DefaultOpacity = 1.0f;
inline constexpr int DefaultFontSize = 16;
inline constexpr int MinFontSize = 8;
inline constexpr int MaxFontSize = 72;
inline constexpr int DefaultMosaicBlockSize = 12;
inline constexpr int MinAnnotationSizePx = 2;
inline constexpr std::size_t MinPenPointCount = 2;

struct ColorBgra
{
  std::uint8_t b{0};
  std::uint8_t g{0};
  std::uint8_t r{220};
  std::uint8_t a{255};
};

struct PointF
{
  float x{0.0f};
  float y{0.0f};
};

struct RectF
{
  float x{0.0f};
  float y{0.0f};
  float width{0.0f};
  float height{0.0f};
};

struct AnnotationStyle
{
  ColorBgra color{};
  float stroke_width{DefaultStrokeWidth};
  float opacity{DefaultOpacity};
  int font_size{DefaultFontSize};
};

struct Annotation
{
  AnnotationType type{AnnotationType::Rectangle};
  AnnotationStyle style{};
  RectF bounds{};
  PointF start{};
  PointF end{};
  std::vector<PointF> points{};
  std::wstring text;
  int mosaic_block_size{DefaultMosaicBlockSize};
};

}  // namespace qingying
