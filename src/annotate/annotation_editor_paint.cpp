#include "annotate/annotation_editor_host.hpp"

#include <algorithm>

#include "annotate/annotation_editor_paint.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <commctrl.h>

#include "annotate/annotation_editor_chrome.h"
#include "annotate/annotation_editor_inline_text.h"

namespace qingying {

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
  std::copy(std::begin(chrome.m_line_style_rects),
            std::end(chrome.m_line_style_rects),
            std::begin(snapshot.line_style_rects));
  snapshot.toolbar_hover = chrome.m_toolbar_hover;
  snapshot.stroke_chip_hover = chrome.m_stroke_chip_hover;
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
  snapshot.text_target_index = inline_text.m_text_target_index;
  snapshot.text_drag_x = inline_text.m_text_drag_x;
  snapshot.text_drag_y = inline_text.m_text_drag_y;
  snapshot.text_anchor_x = inline_text.m_text_anchor_x;
  snapshot.text_anchor_y = inline_text.m_text_anchor_y;
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
  const bool hide_editing =
      snapshot.inline_edit_visible &&
      snapshot.editing_text_index != kInvalidAnnotationIndex &&
      snapshot.editing_text_index < snapshot.document.count();

  if (relocate || hide_editing)
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
