#include "annotate/annotation_editor_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <commctrl.h>

namespace qingying {

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

void paintEditorFrame(HDC hdc, AnnotationEditorHost* data)
{
  if (hdc == nullptr || data == nullptr)
  {
    return;
  }

  const Image& source = data->m_session.source();
  const int origin_x = data->m_image_origin_x;
  const int origin_y = data->m_image_origin_y;
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

void paintEditor(AnnotationEditorHost* data, HDC hdc, bool draw_bar_shells,
                 bool draw_bar_items)
{
  if (data == nullptr || hdc == nullptr)
  {
    return;
  }

  RECT client{};
  GetClientRect(data->m_overlay, &client);
  fillToolbarColorKey(hdc, client);

  Image composed;
  const Annotation* preview =
      data->m_controller.hasPreview() ? &data->m_controller.preview() : nullptr;

  const bool relocate =
      data->m_text_dragging &&
      data->m_text_target_index != kInvalidAnnotationIndex &&
      data->m_text_target_index < data->m_session.engine().document().count();
  const bool hide_editing =
      data->m_inline_edit != nullptr &&
      data->m_editing_text_index != kInvalidAnnotationIndex &&
      data->m_editing_text_index < data->m_session.engine().document().count();

  if (relocate || hide_editing)
  {
    AnnotationDocument temp;
    const auto& items = data->m_session.engine().document().items();
    for (std::size_t i = 0; i < items.size(); ++i)
    {
      if (hide_editing && i == data->m_editing_text_index)
      {
        continue;
      }
      Annotation item = items.at(i);
      if (relocate && i == data->m_text_target_index)
      {
        item.start.x = data->m_text_drag_x;
        item.start.y = data->m_text_drag_y;
        item.bounds.x = item.start.x;
        item.bounds.y = item.start.y;
      }
      (void)temp.add(item);
    }
    if (!data->m_renderer.rasterize(data->m_session.source(), temp, preview,
                                  composed))
    {
      return;
    }
  }
  else if (!data->m_renderer.rasterize(data->m_session.source(),
                                     data->m_session.engine().document(), preview,
                                     composed))
  {
    return;
  }

  blitImage(hdc, composed, data->m_image_origin_x, data->m_image_origin_y);
  paintLiveInlineText(hdc, data);
  paintEditorFrame(hdc, data);
  paintInlineEditFrame(hdc, data);
  drawTextSelectionFrame(hdc, data);
  if (draw_bar_items)
  {
    paintEditorToolbar(hdc, data, draw_bar_shells);
    paintPropertyBar(hdc, data, draw_bar_shells);
  }
}

void paintEditorBuffered(AnnotationEditorHost* data, HDC hdc)
{
  if (data == nullptr || data->m_overlay == nullptr)
  {
    return;
  }
  (void)hdc;

  RECT client{};
  GetClientRect(data->m_overlay, &client);
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
    const HDC window_dc = GetDC(data->m_overlay);
    if (window_dc != nullptr)
    {
      paintEditor(data, window_dc, true);
      ReleaseDC(data->m_overlay, window_dc);
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
  paintEditor(data, mem_dc, false, false);
  applyColorKeyAlpha(bits, width, height, kToolbarColorKey);
  (void)drawToolbarBarOnArgbBits(bits, width, height, data->m_main_bar_rect);
  if (data->m_property_bar_rect.right > data->m_property_bar_rect.left)
  {
    (void)drawToolbarBarOnArgbBits(bits, width, height, data->m_property_bar_rect);
  }
  paintEditorToolbar(mem_dc, data, false);
  paintPropertyBar(mem_dc, data, false);
  promoteRgbToOpaqueAlpha(bits, width, height);
  (void)presentLayeredArgbWindow(data->m_overlay, mem_dc, width, height);
  SelectObject(mem_dc, old_bitmap);
  DeleteDC(mem_dc);
  DeleteObject(dib);
}

void drawTextSelectionFrame(HDC hdc, AnnotationEditorHost* data)
{
  if (hdc == nullptr || data == nullptr ||
      data->m_selected_text_index == kInvalidAnnotationIndex ||
      data->m_selected_text_index >= data->m_session.engine().document().count() ||
      data->m_inline_edit != nullptr)
  {
    return;
  }

  Annotation annotation =
      data->m_session.engine().document().items().at(data->m_selected_text_index);
  if (annotation.type != AnnotationType::Text)
  {
    return;
  }

  const bool dragging =
      data->m_text_dragging &&
      data->m_text_target_index == data->m_selected_text_index;
  const AnnotationEditorTextChrome chrome =
      makeTextChrome(data, annotation, dragging);

  const GdiObject pen(CreatePen(PS_SOLID, AnnotationEditorInlineEditBorderPx,
                                kTextChromeBorderColor));
  if (pen)
  {
    const SelectGuard selected_pen(hdc, pen.get());
    const SelectGuard selected_brush(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, chrome.frame.left, chrome.frame.top, chrome.frame.right,
              chrome.frame.bottom);
  }

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
