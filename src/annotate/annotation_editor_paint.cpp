#include "annotate/annotation_editor_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "annotate/annotation_editor_paint.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <commctrl.h>
#include <gdiplus.h>
#include <objidl.h>

#include "annotate/annotation_editor_chrome.h"
#include "annotate/annotation_editor_inline_text.h"
#include "annotate/annotation_text_rotate_icon_png.h"

namespace qingying {

namespace {

inline constexpr COLORREF TextChromeOuterColor = RGB(36, 39, 44);
inline constexpr COLORREF TextChromeInnerColor = RGB(255, 255, 255);
inline constexpr COLORREF TextRotateButtonFillColor = RGB(45, 105, 235);
inline constexpr int TextChromeOuterWidthPx = 3;
inline constexpr int TextChromeInnerWidthPx = 1;
inline constexpr int TextRotateButtonCornerRadiusPx = 5;
inline constexpr int TextRotateGlyphWidthPx = 2;
inline constexpr int TextRotateGlyphPointCount = 20;
inline constexpr float TextRotateGlyphRadiusPx = 5.0f;
inline constexpr float TextRotateGlyphStartDegrees = 225.0f;
inline constexpr float TextRotateGlyphStepDegrees = -15.0f;
inline constexpr float TextRotateArrowBackPx = 3.0f;
inline constexpr float TextRotateArrowHalfWidthPx = 2.0f;
inline constexpr int TextRotateGlyphInsetPx = 1;

Gdiplus::Color toGdiplusColor(COLORREF color)
{
  return Gdiplus::Color(255, GetRValue(color), GetGValue(color),
                        GetBValue(color));
}

class TextChromeGdiplusSession
{
 public:
  TextChromeGdiplusSession()
  {
    Gdiplus::GdiplusStartupInput input;
    m_ok = (Gdiplus::GdiplusStartup(&m_token, &input, nullptr) == Gdiplus::Ok);
  }

  ~TextChromeGdiplusSession()
  {
    if (m_token != 0)
    {
      Gdiplus::GdiplusShutdown(m_token);
    }
  }

  TextChromeGdiplusSession(const TextChromeGdiplusSession&) = delete;
  TextChromeGdiplusSession& operator=(const TextChromeGdiplusSession&) =
      delete;

  bool ok() const
  {
    return m_ok;
  }

 private:
  ULONG_PTR m_token{0};
  bool m_ok{false};
};

bool ensureTextChromeGdiplus()
{
  static TextChromeGdiplusSession session;
  return session.ok();
}

void configureTextChromeGraphics(Gdiplus::Graphics& graphics)
{
  graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
  graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
}

void addTextChromeRoundRectPath(Gdiplus::GraphicsPath& path,
                                const Gdiplus::RectF& bounds, float radius)
{
  const float diameter = (std::min)(radius * 2.0f,
                                    (std::min)(bounds.Width, bounds.Height));
  path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180.0f, 90.0f);
  path.AddArc(bounds.GetRight() - diameter, bounds.Y, diameter, diameter,
              270.0f, 90.0f);
  path.AddArc(bounds.GetRight() - diameter, bounds.GetBottom() - diameter,
              diameter, diameter, 0.0f, 90.0f);
  path.AddArc(bounds.X, bounds.GetBottom() - diameter, diameter, diameter,
              90.0f, 90.0f);
  path.CloseFigure();
}

struct ComStreamReleaser
{
  void operator()(IStream* stream) const noexcept
  {
    if (stream != nullptr)
    {
      (void)stream->Release();
    }
  }
};

class TextRotatePngCache
{
 public:
  TextRotatePngCache()
  {
    const HGLOBAL stream_data = GlobalAlloc(GMEM_MOVEABLE,
                                            TextRotateIconPngSize);
    if (stream_data == nullptr)
    {
      return;
    }
    void* destination = GlobalLock(stream_data);
    if (destination == nullptr)
    {
      (void)GlobalFree(stream_data);
      return;
    }
    std::memcpy(destination, TextRotateIconPng, TextRotateIconPngSize);
    (void)GlobalUnlock(stream_data);

    IStream* raw_stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(stream_data, TRUE, &raw_stream)))
    {
      (void)GlobalFree(stream_data);
      return;
    }
    m_stream.reset(raw_stream);
    m_image = std::make_unique<Gdiplus::Bitmap>(m_stream.get(), FALSE);
    if (m_image->GetLastStatus() != Gdiplus::Ok)
    {
      m_image.reset();
    }
  }

  TextRotatePngCache(const TextRotatePngCache&) = delete;
  TextRotatePngCache& operator=(const TextRotatePngCache&) = delete;

  Gdiplus::Bitmap* image() const
  {
    return m_image.get();
  }

 private:
  std::unique_ptr<IStream, ComStreamReleaser> m_stream;
  std::unique_ptr<Gdiplus::Bitmap> m_image;
};

bool drawTextRotationPng(Gdiplus::Graphics& graphics,
                         const AnnotationEditorRect& bounds)
{
  static TextRotatePngCache cache;
  Gdiplus::Bitmap* const image = cache.image();
  if (image == nullptr)
  {
    return false;
  }

  const int width = bounds.right - bounds.left + 1;
  const int height = bounds.bottom - bounds.top + 1;
  const int glyph_width = width - TextRotateGlyphInsetPx * 2;
  const int glyph_height = height - TextRotateGlyphInsetPx * 2;
  if (glyph_width <= 0 || glyph_height <= 0)
  {
    return false;
  }
  graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  const Gdiplus::Rect destination_rect(
      bounds.left + TextRotateGlyphInsetPx, bounds.top + TextRotateGlyphInsetPx,
      glyph_width, glyph_height);
  return graphics.DrawImage(image, destination_rect) == Gdiplus::Ok;
}

bool drawTextRotationButtonAntialiased(
    HDC hdc, const AnnotationEditorTextChrome& chrome)
{
  if (hdc == nullptr || !ensureTextChromeGdiplus())
  {
    return false;
  }
  const AnnotationEditorRect& bounds = chrome.rotation_handle_bounds;
  const float width = static_cast<float>(bounds.right - bounds.left + 1);
  const float height = static_cast<float>(bounds.bottom - bounds.top + 1);
  if (width <= 0.0f || height <= 0.0f)
  {
    return false;
  }

  Gdiplus::Graphics graphics(hdc);
  configureTextChromeGraphics(graphics);
  const Gdiplus::RectF button_bounds(static_cast<float>(bounds.left) + 0.5f,
                                     static_cast<float>(bounds.top) + 0.5f,
                                     width - 1.0f, height - 1.0f);
  Gdiplus::GraphicsPath path;
  addTextChromeRoundRectPath(path, button_bounds,
                             static_cast<float>(TextRotateButtonCornerRadiusPx));
  const Gdiplus::SolidBrush fill(toGdiplusColor(TextRotateButtonFillColor));
  Gdiplus::Pen outline(toGdiplusColor(TextChromeInnerColor), 1.0f);
  graphics.FillPath(&fill, &path);
  graphics.DrawPath(&outline, &path);
  return drawTextRotationPng(graphics, bounds);
}

void drawTextRotationButton(HDC hdc,
                            const AnnotationEditorTextChrome& chrome)
{
  if (hdc == nullptr)
  {
    return;
  }
  if (drawTextRotationButtonAntialiased(hdc, chrome))
  {
    return;
  }

  const AnnotationEditorRect& bounds = chrome.rotation_handle_bounds;
  const GdiObject outline_pen(
      CreatePen(PS_SOLID, TextChromeInnerWidthPx, TextChromeInnerColor));
  const GdiObject fill(CreateSolidBrush(TextRotateButtonFillColor));
  if (outline_pen && fill)
  {
    const SelectGuard selected_pen(hdc, outline_pen.get());
    const SelectGuard selected_brush(hdc, fill.get());
    (void)RoundRect(hdc, bounds.left, bounds.top, bounds.right + 1,
                    bounds.bottom + 1, TextRotateButtonCornerRadiusPx,
                    TextRotateButtonCornerRadiusPx);
  }

  const GdiObject glyph_pen(
      CreatePen(PS_SOLID, TextRotateGlyphWidthPx, TextChromeInnerColor));
  if (!glyph_pen)
  {
    return;
  }
  const SelectGuard selected_glyph(hdc, glyph_pen.get());
  const float center_x = chrome.rotation_handle.x;
  const float center_y = chrome.rotation_handle.y;
  std::array<POINT, TextRotateGlyphPointCount> arc{};
  for (int index = 0; index < TextRotateGlyphPointCount; ++index)
  {
    const float degrees =
        TextRotateGlyphStartDegrees + TextRotateGlyphStepDegrees * index;
    const double radians =
        static_cast<double>(degrees) * AnnotationEditorPi / 180.0;
    arc.at(static_cast<std::size_t>(index)).x = static_cast<LONG>(
        std::lround(center_x + TextRotateGlyphRadiusPx * std::cos(radians)));
    arc.at(static_cast<std::size_t>(index)).y = static_cast<LONG>(
        std::lround(center_y + TextRotateGlyphRadiusPx * std::sin(radians)));
  }
  (void)Polyline(hdc, arc.data(), static_cast<int>(arc.size()));

  const POINT& tip = arc.back();
  const double tip_radians =
      static_cast<double>(TextRotateGlyphStartDegrees +
                          TextRotateGlyphStepDegrees *
                              (TextRotateGlyphPointCount - 1)) *
      AnnotationEditorPi / 180.0;
  const float direction_x = static_cast<float>(std::sin(tip_radians));
  const float direction_y = static_cast<float>(-std::cos(tip_radians));
  const float normal_x = -direction_y;
  const float normal_y = direction_x;
  const float back_x = static_cast<float>(tip.x) -
                       direction_x * TextRotateArrowBackPx;
  const float back_y = static_cast<float>(tip.y) -
                       direction_y * TextRotateArrowBackPx;
  POINT arrow[3]{
      POINT{static_cast<LONG>(std::lround(
                back_x + normal_x * TextRotateArrowHalfWidthPx)),
            static_cast<LONG>(std::lround(
                back_y + normal_y * TextRotateArrowHalfWidthPx))},
      tip,
      POINT{static_cast<LONG>(std::lround(
                back_x - normal_x * TextRotateArrowHalfWidthPx)),
            static_cast<LONG>(std::lround(
                back_y - normal_y * TextRotateArrowHalfWidthPx))}};
  (void)Polyline(hdc, arrow, 3);
}

}  // namespace

AnnotationEditorPaintSnapshot makeAnnotationEditorPaintSnapshot(
    const AnnotationEditorHost& data)
{
  AnnotationEditorPaintSnapshot snapshot;
  const AnnotationEditorCoreState& core = data.core();
  const AnnotationEditorWindowState& window = data.window();
  const AnnotationEditorInlineTextState& inline_text = data.inlineText();
  const AnnotationEditorChromeState& chrome = data.chrome();
  const AnnotationEditorStrokePopupState& stroke_popup = data.strokePopup();
  const AnnotationEditorColorPickerState& color_picker = data.colorPicker();

  snapshot.source = core.m_session.source();
  snapshot.document = core.m_session.engine().document();
  if (core.m_controller.hasPreview())
  {
    snapshot.preview = core.m_controller.preview();
  }
  snapshot.overlay = window.m_overlay;
  snapshot.image_origin_x = window.m_image_origin_x;
  snapshot.image_origin_y = window.m_image_origin_y;
  snapshot.tool = core.m_controller.tool();
  snapshot.style = core.m_controller.style();
  snapshot.mosaic_block_size = core.m_controller.mosaicBlockSize();

  std::copy(std::begin(chrome.m_toolbar_items),
            std::end(chrome.m_toolbar_items),
            std::begin(snapshot.toolbar_items));
  snapshot.size_combo_rect = chrome.m_size_combo_rect;
  snapshot.current_color_rect = chrome.m_current_color_rect;
  std::copy(std::begin(chrome.m_color_swatch_rects),
            std::end(chrome.m_color_swatch_rects),
            std::begin(snapshot.color_swatch_rects));
  snapshot.stroke_chip_rect = chrome.m_stroke_chip_rect;
  std::copy(std::begin(chrome.m_shape_rects), std::end(chrome.m_shape_rects),
            std::begin(snapshot.shape_rects));
  snapshot.fill_rect = chrome.m_fill_rect;
  snapshot.bold_rect = chrome.m_bold_rect;
  snapshot.italic_rect = chrome.m_italic_rect;
  snapshot.font_face_rect = chrome.m_font_face_rect;
  snapshot.font_menu_rect = chrome.m_font_menu_rect;
  snapshot.arrow_style_chip_rect = chrome.m_arrow_style_chip_rect;
  snapshot.line_style_chip_rect = chrome.m_line_style_chip_rect;
  snapshot.style_menu_rect = chrome.m_style_menu_rect;
  snapshot.style_menu = chrome.m_style_menu;
  snapshot.font_menu_scroll_offset = chrome.m_font_menu_scroll_offset;
  snapshot.font_menu_open = chrome.m_font_menu_open;
  if (chrome.m_font_menu_open && chrome.m_font_catalog.loaded())
  {
    const std::vector<std::wstring>& fonts =
        chrome.m_font_catalog.cachedFonts();
    const int begin = (std::max)(0, chrome.m_font_menu_scroll_offset);
    const int end = (std::min)(
        static_cast<int>(fonts.size()),
        begin + AnnotationEditorFontMenuMaxVisibleItems);
    for (int i = begin; i < end; ++i)
    {
      snapshot.visible_font_faces.push_back(
          fonts.at(static_cast<std::size_t>(i)));
    }
  }
  snapshot.toolbar_hover = chrome.m_toolbar_hover;
  snapshot.stroke_chip_hover = chrome.m_stroke_chip_hover;
  snapshot.arrow_style_chip_hover = chrome.m_arrow_style_chip_hover;
  snapshot.line_style_chip_hover = chrome.m_line_style_chip_hover;
  snapshot.bold_hover = chrome.m_bold_hover;
  snapshot.italic_hover = chrome.m_italic_hover;
  snapshot.font_face_hover = chrome.m_font_face_hover;
  std::copy(std::begin(chrome.m_toolbar_divider_x),
            std::end(chrome.m_toolbar_divider_x),
            std::begin(snapshot.toolbar_divider_x));
  snapshot.main_bar_rect = chrome.m_main_bar_rect;
  snapshot.property_bar_rect = chrome.m_property_bar_rect;
  snapshot.combo_font = chrome.m_combo_font.asFont();
  snapshot.color_picker_visible =
      color_picker.m_color_picker != nullptr &&
      IsWindowVisible(color_picker.m_color_picker) != FALSE;
  snapshot.stroke_popup_visible =
      stroke_popup.m_stroke_popup != nullptr &&
      IsWindowVisible(stroke_popup.m_stroke_popup) != FALSE;

  snapshot.text_dragging = inline_text.m_text_dragging;
  snapshot.text_rotating = inline_text.m_text_rotating;
  snapshot.text_target_index = inline_text.m_text_target_index;
  snapshot.text_drag_x = inline_text.m_text_drag_x;
  snapshot.text_drag_y = inline_text.m_text_drag_y;
  snapshot.text_rotation_degrees =
      inline_text.m_text_rotation_degrees;
  snapshot.text_anchor_x = inline_text.m_text_anchor_x;
  snapshot.text_anchor_y = inline_text.m_text_anchor_y;
  snapshot.text_wrap_width = inline_text.m_text_wrap_width;
  snapshot.editing_text_index = inline_text.m_editing_text_index;
  snapshot.selected_text_index = inline_text.m_selected_text_index;
  if (inline_text.m_inline_edit != nullptr)
  {
    snapshot.inline_edit_visible = true;
    wchar_t buffer[kInlineTextMaxChars]{};
    GetWindowTextW(inline_text.m_inline_edit, buffer, kInlineTextMaxChars);
    snapshot.inline_text = buffer;

    DWORD selection_start = 0;
    DWORD selection_end = 0;
    (void)SendMessageW(inline_text.m_inline_edit, EM_GETSEL,
                       reinterpret_cast<WPARAM>(&selection_start),
                       reinterpret_cast<LPARAM>(&selection_end));
    const int text_length = static_cast<int>(snapshot.inline_text.size());
    snapshot.inline_caret =
        (std::min)(text_length, (std::max)(0, static_cast<int>(selection_start)));
    const LRESULT caret_position = SendMessageW(
        inline_text.m_inline_edit, EM_POSFROMCHAR,
        static_cast<WPARAM>(snapshot.inline_caret), 0);
    snapshot.inline_caret_position.x =
        static_cast<int>(static_cast<short>(LOWORD(caret_position)));
    snapshot.inline_caret_position.y =
        static_cast<int>(static_cast<short>(HIWORD(caret_position)));

    RECT edit_rect{};
    if (snapshot.overlay != nullptr &&
        GetWindowRect(inline_text.m_inline_edit, &edit_rect) != FALSE)
    {
      MapWindowPoints(HWND_DESKTOP, snapshot.overlay,
                      reinterpret_cast<POINT*>(&edit_rect), 2);
      snapshot.inline_edit_rect = edit_rect;
    }
  }
  return snapshot;
}

void blitImage(HDC hdc, const Image& image, int dest_x, int dest_y)
{
  if (hdc == nullptr || image.empty())
  {
    return;
  }

  BITMAPINFO bmi{};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = image.width;
  bmi.bmiHeader.biHeight = -image.height;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  (void)SetDIBitsToDevice(hdc, dest_x, dest_y, static_cast<DWORD>(image.width),
                          static_cast<DWORD>(image.height), 0, 0, 0,
                          static_cast<UINT>(image.height),
                          image.pixels.data(), &bmi, DIB_RGB_COLORS);
}

void drawTextChromeBorder(HDC hdc, const PointF (&corners)[4])
{
  if (hdc == nullptr)
  {
    return;
  }

  if (ensureTextChromeGdiplus())
  {
    Gdiplus::Graphics graphics(hdc);
    configureTextChromeGraphics(graphics);
    Gdiplus::PointF points[5]{};
    for (int index = 0; index < 4; ++index)
    {
      points[index] = Gdiplus::PointF(corners[index].x, corners[index].y);
    }
    points[4] = points[0];

    Gdiplus::Pen outer(toGdiplusColor(TextChromeOuterColor),
                        static_cast<Gdiplus::REAL>(TextChromeOuterWidthPx));
    outer.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawLines(&outer, points, 5);
    Gdiplus::Pen inner(toGdiplusColor(TextChromeInnerColor),
                        static_cast<Gdiplus::REAL>(TextChromeInnerWidthPx));
    inner.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawLines(&inner, points, 5);
    return;
  }

  POINT points[5]{};
  for (int index = 0; index < 4; ++index)
  {
    points[index].x = static_cast<LONG>(std::lround(corners[index].x));
    points[index].y = static_cast<LONG>(std::lround(corners[index].y));
  }
  points[4] = points[0];

  const GdiObject outer_pen(
      CreatePen(PS_SOLID, TextChromeOuterWidthPx, TextChromeOuterColor));
  if (outer_pen)
  {
    const SelectGuard selected_pen(hdc, outer_pen.get());
    (void)Polyline(hdc, points, 5);
  }
  const GdiObject inner_pen(
      CreatePen(PS_SOLID, TextChromeInnerWidthPx, TextChromeInnerColor));
  if (inner_pen)
  {
    const SelectGuard selected_pen(hdc, inner_pen.get());
    (void)Polyline(hdc, points, 5);
  }
}

void paintEditorFrame(HDC hdc, const AnnotationEditorPaintSnapshot& snapshot)
{
  if (hdc == nullptr)
  {
    return;
  }

  const Image& source = snapshot.source;
  const int origin_x = snapshot.image_origin_x;
  const int origin_y = snapshot.image_origin_y;
  const int image_right = origin_x + source.width;
  const int image_bottom = origin_y + source.height;

  const int border = AnnotationEditorFrameBorderPx;
  const GdiObject border_pen(CreatePen(PS_SOLID, border, kFrameBorderColor));
  if (border_pen)
  {
    const SelectGuard pen(hdc, border_pen.get());
    const SelectGuard brush(hdc, GetStockObject(NULL_BRUSH));
    const int half = border / 2;
    Rectangle(hdc, origin_x - half, origin_y - half, image_right + half + 1,
              image_bottom + half + 1);
  }

  AnnotationEditorHandlePoint handles[AnnotationEditorHandleCount]{};
  annotationEditorHandlePoints(origin_x, origin_y, source.width, source.height,
                               handles);
  const int radius = AnnotationEditorHandleRadiusPx;
  const GdiObject handle_pen(CreatePen(PS_SOLID, 1, kFrameBorderColor));
  const GdiObject handle_brush(CreateSolidBrush(kHandleFillColor));
  if (handle_pen && handle_brush)
  {
    const SelectGuard pen(hdc, handle_pen.get());
    const SelectGuard brush(hdc, handle_brush.get());
    for (int i = 0; i < AnnotationEditorHandleCount; ++i)
    {
      const AnnotationEditorHandlePoint& point =
          handles[static_cast<std::size_t>(i)];
      Ellipse(hdc, point.x - radius, point.y - radius, point.x + radius + 1,
              point.y + radius + 1);
    }
  }

  wchar_t label[32]{};
  if (swprintf_s(label, L"%d x %d px", source.width, source.height) <= 0)
  {
    return;
  }

  const GdiObject font(CreateFontW(
      -kSizeLabelFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace));
  if (!font)
  {
    return;
  }

  const SelectGuard selected_font(hdc, font.get());
  SIZE text_size{};
  const int label_len = lstrlenW(label);
  (void)GetTextExtentPoint32W(hdc, label, label_len, &text_size);
  const int label_left = origin_x;
  const int label_top = origin_y - AnnotationEditorSizeLabelGapPx -
                        AnnotationEditorSizeLabelHeightPx;
  const int label_right =
      label_left + text_size.cx + kSizeLabelPadX * 2;
  const int label_bottom = label_top + AnnotationEditorSizeLabelHeightPx;
  const RECT label_rect{label_left, label_top, label_right, label_bottom};
  const GdiObject label_brush(CreateSolidBrush(kSizeLabelFillColor));
  if (label_brush)
  {
    FillRect(hdc, &label_rect, label_brush.asBrush());
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, kSizeLabelTextColor);
  RECT text_rect = label_rect;
  text_rect.left += kSizeLabelPadX;
  text_rect.top += kSizeLabelPadY;
  DrawTextW(hdc, label, label_len, &text_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

void paintEditor(const AnnotationEditorPaintSnapshot& snapshot, HDC hdc,
                 bool draw_bar_shells,
                 bool draw_bar_items)
{
  if (hdc == nullptr || snapshot.overlay == nullptr)
  {
    return;
  }

  RECT client{};
  GetClientRect(snapshot.overlay, &client);
  fillToolbarColorKey(hdc, client);

  Image composed;
  const Annotation* preview =
      snapshot.preview.has_value() ? &*snapshot.preview : nullptr;

  const bool relocate =
      snapshot.text_dragging &&
      snapshot.text_target_index != kInvalidAnnotationIndex &&
      snapshot.text_target_index < snapshot.document.count();
  const bool rotate =
      snapshot.text_rotating &&
      snapshot.text_target_index != kInvalidAnnotationIndex &&
      snapshot.text_target_index < snapshot.document.count();
  const bool hide_editing =
      snapshot.inline_edit_visible &&
      snapshot.editing_text_index != kInvalidAnnotationIndex &&
      snapshot.editing_text_index < snapshot.document.count();

  if (relocate || rotate || hide_editing)
  {
    AnnotationDocument temp;
    const auto& items = snapshot.document.items();
    for (std::size_t i = 0; i < items.size(); ++i)
    {
      if (hide_editing && i == snapshot.editing_text_index)
      {
        continue;
      }
      Annotation item = items.at(i);
      if (relocate && i == snapshot.text_target_index)
      {
        item.start.x = snapshot.text_drag_x;
        item.start.y = snapshot.text_drag_y;
        item.bounds.x = item.start.x;
        item.bounds.y = item.start.y;
      }
      if (rotate && i == snapshot.text_target_index)
      {
        item.rotation_degrees = snapshot.text_rotation_degrees;
      }
      (void)temp.add(item);
    }
    if (!snapshot.renderer.rasterize(snapshot.source, temp, preview, composed))
    {
      return;
    }
  }
  else if (!snapshot.renderer.rasterize(snapshot.source, snapshot.document,
                                        preview, composed))
  {
    return;
  }

  blitImage(hdc, composed, snapshot.image_origin_x, snapshot.image_origin_y);
  paintLiveInlineText(hdc, snapshot);
  paintEditorFrame(hdc, snapshot);
  paintInlineEditFrame(hdc, snapshot);
  drawTextSelectionFrame(hdc, snapshot);
  if (draw_bar_items)
  {
    paintEditorToolbar(hdc, snapshot, draw_bar_shells);
    paintPropertyBar(hdc, snapshot, draw_bar_shells);
  }
}

void paintEditorBuffered(const AnnotationEditorPaintSnapshot& snapshot, HDC hdc)
{
  if (snapshot.overlay == nullptr)
  {
    return;
  }
  (void)hdc;

  RECT client{};
  GetClientRect(snapshot.overlay, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width <= 0 || height <= 0)
  {
    return;
  }

  void* bits = nullptr;
  const HBITMAP dib = createTopDownArgbDib(width, height, &bits);
  if (dib == nullptr || bits == nullptr)
  {
    const HDC window_dc = GetDC(snapshot.overlay);
    if (window_dc != nullptr)
    {
      paintEditor(snapshot, window_dc, true);
      ReleaseDC(snapshot.overlay, window_dc);
    }
    return;
  }

  const HDC mem_dc = CreateCompatibleDC(nullptr);
  if (mem_dc == nullptr)
  {
    DeleteObject(dib);
    return;
  }

  const HGDIOBJ old_bitmap = SelectObject(mem_dc, dib);
  std::memset(bits, 0, static_cast<std::size_t>(width) *
                            static_cast<std::size_t>(height) * 4u);
  fillToolbarColorKey(mem_dc, client);
  paintEditor(snapshot, mem_dc, false, false);
  applyColorKeyAlpha(bits, width, height, kToolbarColorKey);
  (void)drawToolbarBarOnArgbBits(bits, width, height, snapshot.main_bar_rect);
  if (snapshot.property_bar_rect.right > snapshot.property_bar_rect.left)
  {
    (void)drawToolbarBarOnArgbBits(bits, width, height,
                                   snapshot.property_bar_rect);
  }
  paintEditorToolbar(mem_dc, snapshot, false);
  paintPropertyBar(mem_dc, snapshot, false);
  promoteRgbToOpaqueAlpha(bits, width, height);
  (void)presentLayeredArgbWindow(snapshot.overlay, mem_dc, width, height);
  SelectObject(mem_dc, old_bitmap);
  DeleteDC(mem_dc);
  DeleteObject(dib);
}

void drawTextSelectionFrame(HDC hdc,
                            const AnnotationEditorPaintSnapshot& snapshot)
{
  if (hdc == nullptr ||
      snapshot.selected_text_index == kInvalidAnnotationIndex ||
      snapshot.selected_text_index >= snapshot.document.count() ||
      snapshot.inline_edit_visible)
  {
    return;
  }

  Annotation annotation =
      snapshot.document.items().at(snapshot.selected_text_index);
  if (annotation.type != AnnotationType::Text)
  {
    return;
  }

  const bool dragging =
      snapshot.text_dragging &&
      snapshot.text_target_index == snapshot.selected_text_index;
  const AnnotationEditorTextChrome chrome =
      makeTextChrome(snapshot, annotation, dragging);

  drawTextChromeBorder(hdc, chrome.corners);
  drawTextRotationButton(hdc, chrome);

  RECT delete_rect{chrome.delete_button.left, chrome.delete_button.top,
                   chrome.delete_button.right, chrome.delete_button.bottom};
  const GdiObject delete_fill(CreateSolidBrush(kTextDeleteFillColor));
  if (delete_fill)
  {
    FillRect(hdc, &delete_rect, delete_fill.asBrush());
  }

  const GdiObject glyph(CreatePen(PS_SOLID, kTextDeleteGlyphWidthPx,
                                  kTextDeleteGlyphColor));
  if (!glyph)
  {
    return;
  }
  const SelectGuard selected_glyph(hdc, glyph.get());
  const int inset = kTextDeleteGlyphInsetPx;
  MoveToEx(hdc, delete_rect.left + inset, delete_rect.top + inset, nullptr);
  LineTo(hdc, delete_rect.right - inset, delete_rect.bottom - inset);
  MoveToEx(hdc, delete_rect.right - inset, delete_rect.top + inset, nullptr);
  LineTo(hdc, delete_rect.left + inset, delete_rect.bottom - inset);
}
}  // namespace qingying
