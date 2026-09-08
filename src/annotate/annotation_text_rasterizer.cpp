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

class SavedDcGuard
{
 public:
  explicit SavedDcGuard(HDC dc)
      : m_dc(dc), m_saved(dc != nullptr ? SaveDC(dc) : 0)
  {
  }

  ~SavedDcGuard()
  {
    if (m_dc != nullptr && m_saved != 0)
    {
      (void)RestoreDC(m_dc, m_saved);
    }
  }

  SavedDcGuard(const SavedDcGuard&) = delete;
  SavedDcGuard& operator=(const SavedDcGuard&) = delete;

  bool valid() const
  {
    return m_saved != 0;
  }

 private:
  HDC m_dc{nullptr};
  int m_saved{0};
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

bool measureAnnotationTextLayout(HDC hdc, const Annotation& annotation,
                                 int max_width, SIZE& out_size)
{
  out_size.cx = 0;
  out_size.cy = 0;
  if (hdc == nullptr || annotation.text.empty())
  {
    return false;
  }

  const GdiObject font(createAnnotationTextFont(
      annotation.style, annotation.style.font_size));
  if (!font)
  {
    return false;
  }

  const SelectGuard selected_font(hdc, font.get());
  RECT rect{0, 0, (std::max)(1, max_width), 0};
  const int height = DrawTextW(
      hdc, annotation.text.c_str(), static_cast<int>(annotation.text.size()),
      &rect, DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK | DT_EDITCONTROL |
                 DT_CALCRECT);
  if (height <= 0)
  {
    return false;
  }
  out_size.cx = rect.right - rect.left;
  out_size.cy = rect.bottom - rect.top;
  return out_size.cx > 0 && out_size.cy > 0;
}

bool applyAnnotationTextWorldTransform(HDC hdc, float rotation_degrees,
                                       float center_x, float center_y)
{
  if (hdc == nullptr)
  {
    return false;
  }
  if (std::fabs(rotation_degrees) <= 0.001f)
  {
    return true;
  }
  if (SetGraphicsMode(hdc, GM_ADVANCED) == 0)
  {
    return false;
  }

  const double radians =
      static_cast<double>(rotation_degrees) * 3.14159265358979323846 / 180.0;
  const float cosine = static_cast<float>(std::cos(radians));
  const float sine = static_cast<float>(std::sin(radians));
  const XFORM transform{
      cosine,
      sine,
      -sine,
      cosine,
      center_x - center_x * cosine + center_y * sine,
      center_y - center_x * sine - center_y * cosine};
  return SetWorldTransform(hdc, &transform) != FALSE;
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
  const int remaining_width = (std::max)(1, target.width - x);
  int requested_width = remaining_width;
  if (annotation.text_wrap_width > 0.0f)
  {
    requested_width = toPixel(annotation.text_wrap_width);
  }
  else if (annotation.bounds.width > 0.0f)
  {
    requested_width = toPixel(annotation.bounds.width);
  }
  const int wrap_width =
      (std::max)(1, (std::min)(remaining_width, requested_width));
  SIZE layout_size{};
  (void)measureAnnotationTextLayout(mem_dc.get(), annotation,
                                    (std::max)(1, wrap_width), layout_size);
  RECT rect{x, y, x + (std::max)(1, wrap_width), target.height};
  {
    const SavedDcGuard saved_dc(mem_dc.get());
    const float center_x =
        static_cast<float>(x) + static_cast<float>(wrap_width) / 2.0f;
    const float center_y =
        static_cast<float>(y) + static_cast<float>(layout_size.cy) / 2.0f;
    const bool transform_ready =
        saved_dc.valid() &&
        applyAnnotationTextWorldTransform(
            mem_dc.get(), annotation.rotation_degrees, center_x, center_y);
    if (transform_ready)
    {
      (void)DrawTextW(mem_dc.get(), annotation.text.c_str(),
                      static_cast<int>(annotation.text.size()), &rect,
                      DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK |
                          DT_EDITCONTROL);
    }
  }

  if (old_font != nullptr)
  {
    SelectObject(mem_dc.get(), old_font);
  }
  SelectObject(mem_dc.get(), old_bitmap);

  std::memcpy(target.pixels.data(), bits, byte_count);
}

}  // namespace qingying
