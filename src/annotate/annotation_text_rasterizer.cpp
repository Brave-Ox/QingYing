#include "annotate/annotation_text_rasterizer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace qingying {

namespace {

class GdiDcGuard
{
 public:
  explicit GdiDcGuard(HDC dc) : m_dc(dc) {}
  ~GdiDcGuard()
  {
    if (m_dc != nullptr)
    {
      DeleteDC(m_dc);
    }
  }
  GdiDcGuard(const GdiDcGuard&) = delete;
  GdiDcGuard& operator=(const GdiDcGuard&) = delete;
  HDC get() const { return m_dc; }

 private:
  HDC m_dc{nullptr};
};

class GdiObjectGuard
{
 public:
  explicit GdiObjectGuard(HGDIOBJ obj) : m_obj(obj) {}
  ~GdiObjectGuard()
  {
    if (m_obj != nullptr)
    {
      DeleteObject(m_obj);
    }
  }
  GdiObjectGuard(const GdiObjectGuard&) = delete;
  GdiObjectGuard& operator=(const GdiObjectGuard&) = delete;
  HGDIOBJ get() const { return m_obj; }

 private:
  HGDIOBJ m_obj{nullptr};
};

int clampFontSize(int font_size)
{
  return (std::min)((std::max)(font_size, MinFontSize), MaxFontSize);
}

int toPixel(double value)
{
  return static_cast<int>(std::lround(value));
}

}  // namespace

GdiObject createAnnotationTextFont(const AnnotationStyle& style, int font_size)
{
  const int font_px = clampFontSize(font_size);
  const int font_weight = style.bold ? FW_BOLD : FW_NORMAL;
  const wchar_t* font_face = style.font_face.empty()
                                 ? AnnotationTextFontFace
                                 : style.font_face.c_str();
  return GdiObject(CreateFontW(-font_px, 0, 0, 0, font_weight,
                               style.italic ? TRUE : FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, font_face));
}

void rasterizeAnnotationText(Image& target, const Annotation& annotation)
{
  if (annotation.text.empty() || target.empty())
  {
    return;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = target.width;
  bmi.bmiHeader.biHeight = -target.height;  // 顶置，与 Image 行优先一致
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  const GdiObjectGuard dib(CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS,
                                            &bits, nullptr, 0));
  if (dib.get() == nullptr || bits == nullptr)
  {
    return;
  }

  const std::size_t byte_count =
      static_cast<std::size_t>(target.width) *
      static_cast<std::size_t>(target.height) * sizeof(std::uint32_t);
  std::memcpy(bits, target.pixels.data(), byte_count);

  const GdiDcGuard mem_dc(CreateCompatibleDC(nullptr));
  if (mem_dc.get() == nullptr)
  {
    return;
  }

  const HGDIOBJ old_bitmap = SelectObject(mem_dc.get(), dib.get());
  const GdiObject font(createAnnotationTextFont(
      annotation.style, annotation.style.font_size));
  const HGDIOBJ old_font =
      font.get() != nullptr ? SelectObject(mem_dc.get(), font.get()) : nullptr;

  const ColorBgra& c = annotation.style.color;
  SetTextColor(mem_dc.get(), RGB(c.r, c.g, c.b));
  SetBkMode(mem_dc.get(), TRANSPARENT);

  const int x = toPixel(annotation.start.x);
  const int y = toPixel(annotation.start.y);
  RECT rect{x, y, target.width, target.height};
  DrawTextW(mem_dc.get(), annotation.text.c_str(),
            static_cast<int>(annotation.text.size()), &rect,
            DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);

  if (old_font != nullptr)
  {
    SelectObject(mem_dc.get(), old_font);
  }
  SelectObject(mem_dc.get(), old_bitmap);

  std::memcpy(target.pixels.data(), bits, byte_count);
}

}  // namespace qingying
