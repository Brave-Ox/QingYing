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
inline constexpr int AnnotationStylePresetColorCount = 8;
inline constexpr int AnnotationStylePresetStrokeCount = 3;
inline constexpr float DefaultOpacity = 1.0f;
inline constexpr int DefaultFontSize = 16;
inline constexpr int MinFontSize = 8;
inline constexpr int MaxFontSize = 72;
// 合并前本地文字标注用的字体；编辑态预览与栅格化必须同一张脸，避免系统默认点阵字。
inline constexpr wchar_t AnnotationTextFontFace[] = L"Microsoft YaHei UI";
inline constexpr int DefaultMosaicBlockSize = 12;
inline constexpr int MinMosaicBlockSize = 1;
inline constexpr int MaxMosaicBlockSize = 32;
inline constexpr int MinAnnotationSizePx = 2;

enum class AnnotationLineStyle
{
  Solid,
  Dashed,
  Dotted,
  DashDot,
  DashDotDot,
};

inline constexpr int AnnotationLineStyleCount = 5;
inline constexpr AnnotationLineStyle AnnotationLineStyleOptions[
    AnnotationLineStyleCount] = {AnnotationLineStyle::Solid,
                                 AnnotationLineStyle::Dashed,
                                 AnnotationLineStyle::Dotted,
                                 AnnotationLineStyle::DashDot,
                                 AnnotationLineStyle::DashDotDot};

enum class AnnotationArrowStyle
{
  EndOpen,
  StartOpen,
  BothOpen,
  EndFilled,
  StartFilled,
  BothFilled,
  EndBar,
  StartBar,
  BothBars,
};

inline constexpr int AnnotationArrowStyleCount = 9;
inline constexpr AnnotationArrowStyle AnnotationArrowStyleOptions[
    AnnotationArrowStyleCount] = {
    AnnotationArrowStyle::EndOpen,    AnnotationArrowStyle::StartOpen,
    AnnotationArrowStyle::BothOpen,  AnnotationArrowStyle::EndFilled,
    AnnotationArrowStyle::StartFilled,
    AnnotationArrowStyle::BothFilled, AnnotationArrowStyle::EndBar,
    AnnotationArrowStyle::StartBar,  AnnotationArrowStyle::BothBars};

template <typename Style>
inline constexpr int annotationStyleOptionIndex(const Style* options,
                                                int count, Style style)
{
  if (options == nullptr || count <= 0)
  {
    return -1;
  }
  for (int i = 0; i < count; ++i)
  {
    if (options[i] == style)
    {
      return i;
    }
  }
  return -1;
}

template <typename Style>
inline constexpr Style annotationStyleOptionStep(const Style* options,
                                                 int count, Style style,
                                                 int steps)
{
  if (options == nullptr || count <= 0)
  {
    return style;
  }
  const int current = annotationStyleOptionIndex(options, count, style);
  const int base = current >= 0 ? current : 0;
  int next = (base + steps) % count;
  if (next < 0)
  {
    next += count;
  }
  return options[next];
}

inline constexpr AnnotationLineStyle annotationLineStyleStep(
    AnnotationLineStyle style, int steps)
{
  return annotationStyleOptionStep(AnnotationLineStyleOptions,
                                   AnnotationLineStyleCount, style, steps);
}

inline constexpr AnnotationArrowStyle annotationArrowStyleStep(
    AnnotationArrowStyle style, int steps)
{
  return annotationStyleOptionStep(AnnotationArrowStyleOptions,
                                   AnnotationArrowStyleCount, style, steps);
}
inline constexpr std::size_t MinPenPointCount = 2;

inline int clampMosaicBlockSize(int block_size)
{
  if (block_size < MinMosaicBlockSize)
  {
    return MinMosaicBlockSize;
  }
  if (block_size > MaxMosaicBlockSize)
  {
    return MaxMosaicBlockSize;
  }
  return block_size;
}

struct ColorBgra
{
  std::uint8_t b{0};
  std::uint8_t g{0};
  std::uint8_t r{220};
  std::uint8_t a{255};
};

// Win32 RGB() 同序：0x00BBGGRR，供输入框 WM_CTLCOLOREDIT 使用。
inline constexpr std::uint32_t annotationColorToRgb(const ColorBgra& color)
{
  return static_cast<std::uint32_t>(color.r) |
         (static_cast<std::uint32_t>(color.g) << 8) |
         (static_cast<std::uint32_t>(color.b) << 16);
}

// 红 / 橙 / 黄 / 绿 / 青 / 蓝 / 紫 / 白。首色与 ColorBgra 默认值一致。
inline constexpr ColorBgra AnnotationStylePresetColors[
    AnnotationStylePresetColorCount] = {
    ColorBgra{0, 0, 220, 255},     ColorBgra{0, 140, 255, 255},
    ColorBgra{0, 220, 255, 255},   ColorBgra{40, 180, 0, 255},
    ColorBgra{220, 200, 0, 255},   ColorBgra{255, 80, 30, 255},
    ColorBgra{200, 40, 140, 255},  ColorBgra{255, 255, 255, 255},
};

inline constexpr float AnnotationStylePresetStrokeWidths[
    AnnotationStylePresetStrokeCount] = {2.0f, 4.0f, 8.0f};

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
  std::wstring font_face{AnnotationTextFontFace};
  int font_size{DefaultFontSize};
  bool bold{false};
  bool italic{false};
  bool filled{false};
  AnnotationLineStyle line_style{AnnotationLineStyle::Solid};
  AnnotationArrowStyle arrow_style{AnnotationArrowStyle::EndOpen};
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
  float text_wrap_width{0.0f};
  float rotation_degrees{0.0f};
  int mosaic_block_size{DefaultMosaicBlockSize};
};

}  // namespace qingying
