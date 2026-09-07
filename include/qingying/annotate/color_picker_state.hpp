#pragma once

#include "qingying/action/image.hpp"
#include "qingying/annotate/color_convert.hpp"

namespace qingying {

class ColorPickerState
{
 public:
  void open(const ColorBgra& committed)
  {
    m_committed = committed;
    m_draft = committed;
    rememberHueFromDraft();
  }

  void setFormat(ColorPickerFormat format)
  {
    m_format = format;
  }

  ColorPickerFormat format() const
  {
    return m_format;
  }

  const ColorBgra& draft() const
  {
    return m_draft;
  }

  const ColorBgra& committed() const
  {
    return m_committed;
  }

  int hueDegrees() const
  {
    return m_hue_degrees;
  }

  void setHueDegrees(int hue)
  {
    m_hue_degrees = clampInt(hue, 0, ColorHueMaxDegrees);
    const ColorHsv hsv = rgbToHsv(m_draft);
    m_draft = hsvToRgb(m_hue_degrees, hsv.saturation, hsv.value, m_draft.a);
  }

  void setSaturationValue(int saturation, int value)
  {
    m_draft = hsvToRgb(m_hue_degrees, saturation, value, m_draft.a);
  }

  void setRgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
  {
    m_draft.r = r;
    m_draft.g = g;
    m_draft.b = b;
    rememberHueFromDraft();
  }

  void setAlpha(std::uint8_t alpha)
  {
    m_draft.a = alpha;
  }

  void sampleOpaqueRgb(const ColorBgra& sampled)
  {
    m_draft.r = sampled.r;
    m_draft.g = sampled.g;
    m_draft.b = sampled.b;
    m_draft.a = static_cast<std::uint8_t>(ColorChannelMax);
    rememberHueFromDraft();
  }

  ColorBgra confirm() const
  {
    return m_draft;
  }

 private:
  void rememberHueFromDraft()
  {
    const ColorHsv hsv = rgbToHsv(m_draft);
    if (hsv.saturation > 0 && hsv.value > 0)
    {
      m_hue_degrees = hsv.hue_degrees;
    }
  }

  ColorBgra m_committed{};
  ColorBgra m_draft{};
  ColorPickerFormat m_format{ColorPickerFormat::Hex};
  int m_hue_degrees{0};
};

inline bool sampleImageBgra(const Image& image, int x, int y, ColorBgra& out)
{
  if (image.empty() || x < 0 || y < 0 || x >= image.width || y >= image.height)
  {
    return false;
  }
  const std::size_t index = static_cast<std::size_t>(y) *
                                static_cast<std::size_t>(image.width) +
                            static_cast<std::size_t>(x);
  if (index >= image.pixels.size())
  {
    return false;
  }
  out = unpackColorBgra(image.pixels.at(index));
  return true;
}

inline bool sampleImageBgraAtClientPoint(const Image& image, int client_x,
                                         int client_y, int image_origin_x,
                                         int image_origin_y, ColorBgra& out)
{
  return sampleImageBgra(image, client_x - image_origin_x,
                         client_y - image_origin_y, out);
}

inline bool sampleImageBgraAtScreenPoint(const Image& image, int image_screen_x,
                                         int image_screen_y, int screen_x,
                                         int screen_y, ColorBgra& out)
{
  return sampleImageBgra(image, screen_x - image_screen_x,
                         screen_y - image_screen_y, out);
}

inline bool sampleColorPickerAtClientPoint(ColorPickerState& state,
                                           const Image& image, int client_x,
                                           int client_y, int image_origin_x,
                                           int image_origin_y)
{
  ColorBgra sampled{};
  if (!sampleImageBgraAtClientPoint(image, client_x, client_y, image_origin_x,
                                    image_origin_y, sampled))
  {
    return false;
  }
  state.sampleOpaqueRgb(sampled);
  return true;
}

inline bool win32ColorRefToBgra(std::uint32_t color_ref, ColorBgra& out)
{
  constexpr std::uint32_t InvalidColorRef = 0xFFFFFFFFu;
  if (color_ref == InvalidColorRef)
  {
    return false;
  }
  out.r = static_cast<std::uint8_t>(color_ref & 0xFFu);
  out.g = static_cast<std::uint8_t>((color_ref >> 8) & 0xFFu);
  out.b = static_cast<std::uint8_t>((color_ref >> 16) & 0xFFu);
  out.a = static_cast<std::uint8_t>(ColorChannelMax);
  return true;
}

}  // namespace qingying
