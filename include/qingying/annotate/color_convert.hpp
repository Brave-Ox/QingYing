#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwchar>

#include "qingying/annotate/annotation_types.hpp"

namespace qingying {

inline constexpr int ColorHueMaxDegrees = 360;
inline constexpr int ColorPercentMax = 100;
inline constexpr int ColorChannelMax = 255;
inline constexpr int ColorHexTextMaxChars = 8;
inline constexpr int ColorRgbHsvRoundTripTolerance = 1;

enum class ColorPickerFormat
{
  Hex,
  Rgb,
  Hsv,
};

struct ColorHsv
{
  int hue_degrees{0};
  int saturation{0};
  int value{0};
};

inline int clampInt(int value, int lo, int hi)
{
  if (value < lo)
  {
    return lo;
  }
  if (value > hi)
  {
    return hi;
  }
  return value;
}

inline std::uint8_t roundToChannel(float value)
{
  const int rounded = static_cast<int>(std::lround(value));
  return static_cast<std::uint8_t>(clampInt(rounded, 0, ColorChannelMax));
}

inline ColorBgra hsvToRgb(int hue_degrees, int saturation, int value,
                          std::uint8_t alpha)
{
  const int hue = clampInt(hue_degrees, 0, ColorHueMaxDegrees) % ColorHueMaxDegrees;
  const float s =
      static_cast<float>(clampInt(saturation, 0, ColorPercentMax)) /
      static_cast<float>(ColorPercentMax);
  const float v =
      static_cast<float>(clampInt(value, 0, ColorPercentMax)) /
      static_cast<float>(ColorPercentMax);
  const float c = v * s;
  const float hue_sector = static_cast<float>(hue) / 60.0f;
  const float x = c * (1.0f - std::fabs(std::fmod(hue_sector, 2.0f) - 1.0f));
  const float m = v - c;

  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  if (hue_sector < 1.0f)
  {
    r = c;
    g = x;
  }
  else if (hue_sector < 2.0f)
  {
    r = x;
    g = c;
  }
  else if (hue_sector < 3.0f)
  {
    g = c;
    b = x;
  }
  else if (hue_sector < 4.0f)
  {
    g = x;
    b = c;
  }
  else if (hue_sector < 5.0f)
  {
    r = x;
    b = c;
  }
  else
  {
    r = c;
    b = x;
  }

  ColorBgra color{};
  color.r = roundToChannel((r + m) * static_cast<float>(ColorChannelMax));
  color.g = roundToChannel((g + m) * static_cast<float>(ColorChannelMax));
  color.b = roundToChannel((b + m) * static_cast<float>(ColorChannelMax));
  color.a = alpha;
  return color;
}

inline ColorHsv rgbToHsv(const ColorBgra& color)
{
  const float r = static_cast<float>(color.r) / static_cast<float>(ColorChannelMax);
  const float g = static_cast<float>(color.g) / static_cast<float>(ColorChannelMax);
  const float b = static_cast<float>(color.b) / static_cast<float>(ColorChannelMax);
  const float maxc = (std::max)(r, (std::max)(g, b));
  const float minc = (std::min)(r, (std::min)(g, b));
  const float delta = maxc - minc;

  ColorHsv hsv{};
  hsv.value = clampInt(
      static_cast<int>(std::lround(maxc * static_cast<float>(ColorPercentMax))),
      0, ColorPercentMax);
  if (maxc <= 0.0f)
  {
    hsv.saturation = 0;
    hsv.hue_degrees = 0;
    return hsv;
  }
  hsv.saturation = clampInt(
      static_cast<int>(std::lround((delta / maxc) * static_cast<float>(ColorPercentMax))),
      0, ColorPercentMax);
  if (delta <= 0.0f)
  {
    hsv.hue_degrees = 0;
    return hsv;
  }

  float hue = 0.0f;
  if (maxc == r)
  {
    hue = 60.0f * std::fmod((g - b) / delta, 6.0f);
  }
  else if (maxc == g)
  {
    hue = 60.0f * ((b - r) / delta + 2.0f);
  }
  else
  {
    hue = 60.0f * ((r - g) / delta + 4.0f);
  }
  if (hue < 0.0f)
  {
    hue += static_cast<float>(ColorHueMaxDegrees);
  }
  hsv.hue_degrees = clampInt(static_cast<int>(std::lround(hue)), 0,
                             ColorHueMaxDegrees - 1);
  return hsv;
}

inline int hexNibble(wchar_t ch)
{
  if (ch >= L'0' && ch <= L'9')
  {
    return ch - L'0';
  }
  if (ch >= L'a' && ch <= L'f')
  {
    return ch - L'a' + 10;
  }
  if (ch >= L'A' && ch <= L'F')
  {
    return ch - L'A' + 10;
  }
  return -1;
}

inline bool parseHex(const wchar_t* text, ColorBgra& inout)
{
  if (text == nullptr || text[0] != L'#')
  {
    return false;
  }

  int length = 0;
  while (text[length + 1] != L'\0')
  {
    ++length;
    if (length > 6)
    {
      return false;
    }
  }

  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  if (length == 3)
  {
    const int rn = hexNibble(text[1]);
    const int gn = hexNibble(text[2]);
    const int bn = hexNibble(text[3]);
    if (rn < 0 || gn < 0 || bn < 0)
    {
      return false;
    }
    r = static_cast<std::uint8_t>(rn * 17);
    g = static_cast<std::uint8_t>(gn * 17);
    b = static_cast<std::uint8_t>(bn * 17);
  }
  else if (length == 6)
  {
    const int r1 = hexNibble(text[1]);
    const int r0 = hexNibble(text[2]);
    const int g1 = hexNibble(text[3]);
    const int g0 = hexNibble(text[4]);
    const int b1 = hexNibble(text[5]);
    const int b0 = hexNibble(text[6]);
    if (r1 < 0 || r0 < 0 || g1 < 0 || g0 < 0 || b1 < 0 || b0 < 0)
    {
      return false;
    }
    r = static_cast<std::uint8_t>((r1 << 4) | r0);
    g = static_cast<std::uint8_t>((g1 << 4) | g0);
    b = static_cast<std::uint8_t>((b1 << 4) | b0);
  }
  else
  {
    return false;
  }

  inout.r = r;
  inout.g = g;
  inout.b = b;
  return true;
}

inline bool formatHex(const ColorBgra& color, wchar_t* out, int out_chars)
{
  if (out == nullptr || out_chars < ColorHexTextMaxChars)
  {
    return false;
  }
  if (swprintf_s(out, static_cast<std::size_t>(out_chars), L"#%02x%02x%02x",
                 static_cast<unsigned int>(color.r),
                 static_cast<unsigned int>(color.g),
                 static_cast<unsigned int>(color.b)) < 0)
  {
    out[0] = L'\0';
    return false;
  }
  return true;
}

inline bool parseSignedInt(const wchar_t* text, int& out)
{
  if (text == nullptr || text[0] == L'\0')
  {
    return false;
  }
  std::size_t index = 0;
  bool negative = false;
  if (text[0] == L'-')
  {
    negative = true;
    index = 1;
    if (text[index] == L'\0')
    {
      return false;
    }
  }
  int value = 0;
  bool any = false;
  for (; text[index] != L'\0'; ++index)
  {
    if (text[index] < L'0' || text[index] > L'9')
    {
      return false;
    }
    any = true;
    if (value > 100000)
    {
      value = 100000;
      continue;
    }
    value = value * 10 + static_cast<int>(text[index] - L'0');
  }
  if (!any)
  {
    return false;
  }
  out = negative ? -value : value;
  return true;
}

inline bool parseChannel255(const wchar_t* text, int& out)
{
  int value = 0;
  if (!parseSignedInt(text, value))
  {
    return false;
  }
  out = clampInt(value, 0, ColorChannelMax);
  return true;
}

inline bool parsePercent(const wchar_t* text, int& out)
{
  if (text == nullptr || text[0] == L'\0')
  {
    return false;
  }
  wchar_t buffer[16]{};
  std::size_t length = 0;
  while (text[length] != L'\0')
  {
    ++length;
    if (length >= 16)
    {
      return false;
    }
  }
  if (text[length - 1] == L'%')
  {
    if (length <= 1)
    {
      return false;
    }
    for (std::size_t i = 0; i < length - 1; ++i)
    {
      buffer[i] = text[i];
    }
    buffer[length - 1] = L'\0';
    text = buffer;
  }
  int value = 0;
  if (!parseSignedInt(text, value))
  {
    return false;
  }
  out = clampInt(value, 0, ColorPercentMax);
  return true;
}

inline bool parseHue(const wchar_t* text, int& out)
{
  int value = 0;
  if (!parseSignedInt(text, value))
  {
    return false;
  }
  out = clampInt(value, 0, ColorHueMaxDegrees);
  return true;
}

inline std::uint32_t packColorBgra(const ColorBgra& color)
{
  return (static_cast<std::uint32_t>(color.a) << 24) |
         (static_cast<std::uint32_t>(color.r) << 16) |
         (static_cast<std::uint32_t>(color.g) << 8) |
         static_cast<std::uint32_t>(color.b);
}

inline ColorBgra unpackColorBgra(std::uint32_t packed)
{
  ColorBgra color{};
  color.b = static_cast<std::uint8_t>(packed);
  color.g = static_cast<std::uint8_t>(packed >> 8);
  color.r = static_cast<std::uint8_t>(packed >> 16);
  color.a = static_cast<std::uint8_t>(packed >> 24);
  return color;
}

inline std::uint32_t blendSrcOver(std::uint32_t dst, const ColorBgra& src)
{
  if (src.a == 0)
  {
    return dst;
  }
  if (src.a == ColorChannelMax)
  {
    return packColorBgra(src);
  }

  const ColorBgra bottom = unpackColorBgra(dst);
  const int inv = ColorChannelMax - static_cast<int>(src.a);
  ColorBgra out{};
  out.r = static_cast<std::uint8_t>(
      (static_cast<int>(src.r) * src.a + static_cast<int>(bottom.r) * inv + 127) /
      ColorChannelMax);
  out.g = static_cast<std::uint8_t>(
      (static_cast<int>(src.g) * src.a + static_cast<int>(bottom.g) * inv + 127) /
      ColorChannelMax);
  out.b = static_cast<std::uint8_t>(
      (static_cast<int>(src.b) * src.a + static_cast<int>(bottom.b) * inv + 127) /
      ColorChannelMax);
  out.a = static_cast<std::uint8_t>(ColorChannelMax);
  return packColorBgra(out);
}

inline bool colorsMatchRgb(const ColorBgra& left, const ColorBgra& right)
{
  return left.r == right.r && left.g == right.g && left.b == right.b;
}

inline bool colorMatchesAnyPresetRgb(const ColorBgra& color)
{
  for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
  {
    if (colorsMatchRgb(color,
                       AnnotationStylePresetColors[static_cast<std::size_t>(i)]))
    {
      return true;
    }
  }
  return false;
}

}  // namespace qingying
