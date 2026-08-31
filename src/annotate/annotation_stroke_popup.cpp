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

int currentStrokeWidthPx(const AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return static_cast<int>(DefaultStrokeWidth);
  }
  return annotationEditorStrokeWidthPx(data->m_controller.style().stroke_width);
}

void syncStrokePopupEdit(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_stroke_popup_edit == nullptr ||
      data->m_stroke_syncing)
  {
    return;
  }

  wchar_t wanted[8]{};
  (void)swprintf_s(wanted, L"%d", currentStrokeWidthPx(data));
  wchar_t current[16]{};
  GetWindowTextW(data->m_stroke_popup_edit, current,
                 static_cast<int>(sizeof(current) / sizeof(current[0])));
  if (lstrcmpW(current, wanted) == 0)
  {
    return;
  }

  data->m_stroke_syncing = true;
  SetWindowTextW(data->m_stroke_popup_edit, wanted);
  data->m_stroke_syncing = false;
}

void applyEditorStrokeWidth(AnnotationEditorHost* data, int width)
{
  if (data == nullptr)
  {
    return;
  }

  const int clamped = annotationEditorClampStrokeWidthPx(width);
  data->m_controller.setStrokeWidth(static_cast<float>(clamped));
  if (data->m_controller.isDrawing())
  {
    invalidateImageArea(data);
  }
  invalidateToolbar(data);
  if (data->m_stroke_popup != nullptr &&
      IsWindowVisible(data->m_stroke_popup) != FALSE)
  {
    InvalidateRect(data->m_stroke_popup, nullptr, FALSE);
  }
}

bool handleStrokeChipWheel(AnnotationEditorHost* data, int delta)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsStroke(data->m_controller.tool()))
  {
    return false;
  }

  const int steps = annotationEditorWheelDeltaToSteps(delta);
  applyEditorStrokeWidth(
      data, annotationEditorStepStrokeWidth(currentStrokeWidthPx(data), steps));
  syncStrokePopupEdit(data);
  return true;
}

void hideStrokePopup(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->m_stroke_slider_dragging = false;
  if (data->m_stroke_popup != nullptr)
  {
    ShowWindow(data->m_stroke_popup, SW_HIDE);
  }
}

void destroyStrokePopup(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->m_stroke_slider_dragging = false;
  data->m_stroke_popup_edit = nullptr;
  if (data->m_stroke_popup != nullptr)
  {
    DestroyWindow(data->m_stroke_popup);
    data->m_stroke_popup = nullptr;
  }
}

void applyStrokeFromPopupSlider(AnnotationEditorHost* data, int client_x)
{
  if (data == nullptr)
  {
    return;
  }
  const AnnotationEditorStrokePopupLayout layout =
      annotationEditorStrokePopupLayout();
  const int width = (std::max)(AnnotationEditorStrokeSliderMinExtentPx,
                               layout.slider.right - layout.slider.left);
  applyEditorStrokeWidth(
      data, annotationEditorStrokeSliderValue(client_x, layout.slider.left,
                                              width));
  syncStrokePopupEdit(data);
}

void paintStrokePopup(HWND hwnd, AnnotationEditorHost* data)
{
  if (hwnd == nullptr || data == nullptr)
  {
    return;
  }

  RECT client{};
  GetClientRect(hwnd, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  const PaintGuard paint(hwnd);
  if (width <= 0 || height <= 0)
  {
    return;
  }

  void* bits = nullptr;
  const HBITMAP dib = createTopDownArgbDib(width, height, &bits);
  if (dib == nullptr || bits == nullptr)
  {
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
  (void)drawToolbarBarOnArgbBits(bits, width, height, client);

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const AnnotationEditorStrokePopupLayout layout =
      annotationEditorStrokePopupLayout();
  RECT label = toWinRect(layout.label);
  RECT slider = toWinRect(layout.slider);
  RECT hint = toWinRect(layout.hint);

  HFONT font = data->m_combo_font.asFont();
  const HGDIOBJ old_font =
      (font != nullptr) ? SelectObject(mem_dc, font) : nullptr;
  SetBkMode(mem_dc, TRANSPARENT);
  SetTextColor(mem_dc, colors.label);
  DrawTextW(mem_dc, L"\x753B\x7B14", -1, &label,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  fillRoundRect(mem_dc, slider, kStrokeSliderTrackColor, kStrokeSliderTrackColor,
                slider.bottom - slider.top);

  const int value = currentStrokeWidthPx(data);
  const int track_w =
      (std::max)(AnnotationEditorStrokeSliderMinExtentPx,
                 static_cast<int>(slider.right - slider.left));
  const int thumb_x =
      annotationEditorStrokeSliderX(value, slider.left, track_w);
  RECT fill = slider;
  fill.right =
      (std::max)(static_cast<int>(slider.left) +
                     AnnotationEditorStrokeSliderMinExtentPx,
                 thumb_x);
  fillRoundRect(mem_dc, fill, kStrokeSliderFillColor, kStrokeSliderFillColor,
                slider.bottom - slider.top);

  const int thumb = AnnotationEditorStrokeSliderThumbPx;
  const int cy = (slider.top + slider.bottom) / 2;
  const HPEN thumb_pen = CreatePen(PS_SOLID, 1, kStrokeSliderFillColor);
  const HBRUSH thumb_brush = CreateSolidBrush(kStrokeSliderThumbFill);
  if (thumb_pen != nullptr && thumb_brush != nullptr)
  {
    const HGDIOBJ old_pen = SelectObject(mem_dc, thumb_pen);
    const HGDIOBJ old_brush = SelectObject(mem_dc, thumb_brush);
    Ellipse(mem_dc, thumb_x - thumb / 2, cy - thumb / 2, thumb_x + thumb / 2 + 1,
            cy + thumb / 2 + 1);
    SelectObject(mem_dc, old_brush);
    SelectObject(mem_dc, old_pen);
  }
  if (thumb_pen != nullptr)
  {
    DeleteObject(thumb_pen);
  }
  if (thumb_brush != nullptr)
  {
    DeleteObject(thumb_brush);
  }

  SetTextColor(mem_dc, kStrokePopupHintColor);
  DrawTextW(mem_dc, L"\x4E5F\x53EF\x4EE5\x7528\x6EDA\x8F6E\x8C03\x6574\x3002",
            -1, &hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  if (old_font != nullptr)
  {
    SelectObject(mem_dc, old_font);
  }

  promoteRgbToOpaqueAlpha(bits, width, height);
  (void)presentLayeredArgbWindow(hwnd, mem_dc, width, height);
  SelectObject(mem_dc, old_bitmap);
  DeleteDC(mem_dc);
  DeleteObject(dib);
}

LRESULT CALLBACK strokePopupWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam)
{
  AnnotationEditorHost* data = reinterpret_cast<AnnotationEditorHost*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg)
  {
    case WM_NCCREATE:
    {
      const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      return TRUE;
    }
    case WM_PAINT:
      paintStrokePopup(hwnd, data);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_CTLCOLOREDIT:
    {
      const HDC hdc = reinterpret_cast<HDC>(wparam);
      SetTextColor(hdc, DefaultModernToolbarColors.label);
      SetBkColor(hdc, DefaultModernToolbarColors.bar_fill);
      return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    }
    case WM_LBUTTONDOWN:
    {
      if (data == nullptr)
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      const AnnotationEditorStrokePopupLayout layout =
          annotationEditorStrokePopupLayout();
      const RECT slider = toWinRect(layout.slider);
      RECT hit = slider;
      hit.top -= AnnotationEditorStrokeSliderThumbPx;
      hit.bottom += AnnotationEditorStrokeSliderThumbPx;
      if (PtInRect(&hit, POINT{x, y}) != FALSE)
      {
        data->m_stroke_slider_dragging = true;
        SetCapture(hwnd);
        applyStrokeFromPopupSlider(data, x);
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      if (data != nullptr && data->m_stroke_slider_dragging)
      {
        const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
        applyStrokeFromPopupSlider(data, x);
      }
      return 0;
    case WM_LBUTTONUP:
      if (data != nullptr)
      {
        data->m_stroke_slider_dragging = false;
      }
      ReleaseCapture();
      return 0;
    case WM_MOUSEWHEEL:
      if (data != nullptr)
      {
        (void)handleStrokeChipWheel(
            data, static_cast<int>(static_cast<short>(HIWORD(wparam))));
      }
      return 0;
    case WM_COMMAND:
    {
      if (data == nullptr || data->m_stroke_syncing)
      {
        return 0;
      }
      const UINT id = LOWORD(wparam);
      const UINT code = HIWORD(wparam);
      if (id != kStrokePopupEditId)
      {
        return 0;
      }
      if (code == EN_CHANGE)
      {
        wchar_t text[16]{};
        GetWindowTextW(data->m_stroke_popup_edit, text,
                       static_cast<int>(sizeof(text) / sizeof(text[0])));
        int width = 0;
        if (annotationEditorParseStrokeWidthText(text, width))
        {
          applyEditorStrokeWidth(data, width);
        }
      }
      else if (code == EN_KILLFOCUS)
      {
        syncStrokePopupEdit(data);
      }
      return 0;
    }
    case WM_ACTIVATE:
      if (data != nullptr && LOWORD(wparam) == WA_INACTIVE)
      {
        POINT pt{};
        GetCursorPos(&pt);
        ScreenToClient(data->m_overlay, &pt);
        if (!hitTestStrokeChip(data, pt.x, pt.y))
        {
          hideStrokePopup(data);
        }
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool registerStrokePopupClass(HINSTANCE instance)
{
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = strokePopupWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
  wc.lpszClassName = kStrokePopupClassName;
  return RegisterClassExW(&wc) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void showStrokePopup(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_overlay == nullptr ||
      !annotationEditorPropertyBarShowsStroke(data->m_controller.tool()))
  {
    return;
  }

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerStrokePopupClass(instance))
  {
    return;
  }

  RECT chip = data->m_stroke_chip_rect;
  POINT origin{chip.left, chip.bottom + AnnotationEditorButtonGap};
  ClientToScreen(data->m_overlay, &origin);

  if (data->m_stroke_popup == nullptr)
  {
    data->m_stroke_popup = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kStrokePopupClassName,
        L"", WS_POPUP | WS_CLIPCHILDREN, origin.x, origin.y,
        AnnotationEditorStrokePopupWidth, AnnotationEditorStrokePopupHeight,
        data->m_overlay, nullptr, instance, data);
    if (data->m_stroke_popup == nullptr)
    {
      return;
    }

    const AnnotationEditorStrokePopupLayout layout =
        annotationEditorStrokePopupLayout();
    const RECT value = toWinRect(layout.value);
    POINT edit_origin{value.left, value.top};
    ClientToScreen(data->m_stroke_popup, &edit_origin);
    data->m_stroke_popup_edit = CreateWindowExW(
        WS_EX_TOOLWINDOW, L"EDIT", L"",
        WS_POPUP | WS_VISIBLE | ES_NUMBER | ES_CENTER, edit_origin.x,
        edit_origin.y, value.right - value.left, value.bottom - value.top,
        data->m_stroke_popup, nullptr, instance, nullptr);
    if (data->m_stroke_popup_edit != nullptr)
    {
      SetWindowLongPtrW(data->m_stroke_popup_edit, GWLP_ID,
                        static_cast<LONG_PTR>(kStrokePopupEditId));
    }
    if (data->m_stroke_popup_edit != nullptr && data->m_combo_font)
    {
      SendMessageW(data->m_stroke_popup_edit, WM_SETFONT,
                   reinterpret_cast<WPARAM>(data->m_combo_font.get()), TRUE);
    }
  }
  else
  {
    SetWindowPos(data->m_stroke_popup, HWND_TOPMOST, origin.x, origin.y,
                 AnnotationEditorStrokePopupWidth,
                 AnnotationEditorStrokePopupHeight, SWP_NOACTIVATE);
  }

  syncStrokePopupEdit(data);
  ShowWindow(data->m_stroke_popup, SW_SHOW);
  SetForegroundWindow(data->m_stroke_popup);
  if (data->m_stroke_popup_edit != nullptr)
  {
    SetFocus(data->m_stroke_popup_edit);
    SendMessageW(data->m_stroke_popup_edit, EM_SETSEL, 0, -1);
  }
}
}  // namespace qingying
