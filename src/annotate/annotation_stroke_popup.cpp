#include "annotate/annotation_editor_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include <commctrl.h>

#include "annotate/annotation_editor_chrome.h"
#include "annotate/annotation_editor_stroke_popup.h"

namespace qingying {

namespace {

struct StrokePopupPaintSnapshot
{
  HFONT combo_font{nullptr};
  int width_px{static_cast<int>(DefaultStrokeWidth)};
};

}  // namespace

int currentStrokeWidthPx(const AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return static_cast<int>(DefaultStrokeWidth);
  }
  return annotationEditorStrokeWidthPx(data->core().m_controller.style().stroke_width);
}

void syncStrokePopupEdit(AnnotationEditorHost* data)
{
  if (data == nullptr || data->strokePopup().m_stroke_popup_edit == nullptr ||
      data->strokePopup().m_stroke_syncing)
  {
    return;
  }

  wchar_t wanted[AnnotationEditorStrokePopupValueTextMaxChars]{};
  if (!annotationEditorStrokePopupFormatWidthText(
          currentStrokeWidthPx(data), wanted,
          AnnotationEditorStrokePopupValueTextMaxChars))
  {
    return;
  }
  wchar_t current[AnnotationEditorStrokePopupValueTextMaxChars]{};
  if (GetWindowTextW(data->strokePopup().m_stroke_popup_edit, current,
                     AnnotationEditorStrokePopupValueTextMaxChars) < 0)
  {
    current[0] = L'\0';
  }
  if (lstrcmpW(current, wanted) == 0)
  {
    return;
  }

  data->strokePopup().m_stroke_syncing = true;
  if (SetWindowTextW(data->strokePopup().m_stroke_popup_edit, wanted) == FALSE)
  {
    data->strokePopup().m_stroke_syncing = false;
    return;
  }
  if (InvalidateRect(data->strokePopup().m_stroke_popup_edit, nullptr, FALSE) == FALSE)
  {
    // 文本已写入，重绘失败时下次滑条/失焦会再刷。
  }
  data->strokePopup().m_stroke_syncing = false;
}

void applyStrokeFromPopupEdit(AnnotationEditorHost* data)
{
  if (data == nullptr || data->strokePopup().m_stroke_popup_edit == nullptr ||
      data->strokePopup().m_stroke_syncing)
  {
    return;
  }

  wchar_t text[AnnotationEditorStrokePopupValueTextMaxChars]{};
  if (GetWindowTextW(data->strokePopup().m_stroke_popup_edit, text,
                     AnnotationEditorStrokePopupValueTextMaxChars) <= 0)
  {
    return;
  }

  int width = 0;
  if (!annotationEditorParseStrokeWidthText(text, width))
  {
    return;
  }
  applyEditorStrokeWidth(data, width);
}

bool handleStrokePopupEditCommand(AnnotationEditorHost* data, UINT id, UINT code,
                                  HWND control)
{
  if (data == nullptr || data->strokePopup().m_stroke_popup_edit == nullptr)
  {
    return false;
  }
  const bool hwnd_matches = control == data->strokePopup().m_stroke_popup_edit;
  if (!annotationEditorStrokePopupAcceptsEditNotification(
          id, kStrokePopupEditId, hwnd_matches))
  {
    return false;
  }
  if (code == EN_CHANGE)
  {
    applyStrokeFromPopupEdit(data);
    return true;
  }
  if (code == EN_KILLFOCUS)
  {
    syncStrokePopupEdit(data);
    return true;
  }
  return false;
}

LRESULT CALLBACK strokePopupEditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                             LPARAM lparam, UINT_PTR subclass_id,
                                             DWORD_PTR ref_data)
{
  AnnotationEditorHost* data =
      reinterpret_cast<AnnotationEditorHost*>(ref_data);
  if (msg == WM_NCDESTROY)
  {
    if (RemoveWindowSubclass(hwnd, strokePopupEditSubclassProc, subclass_id) ==
        FALSE)
    {
      // 销毁路径上子类可能已卸。
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
  }

  const LRESULT result = DefSubclassProc(hwnd, msg, wparam, lparam);
  if (msg == WM_CHAR || msg == WM_CUT || msg == WM_PASTE || msg == WM_CLEAR ||
      msg == WM_UNDO)
  {
    applyStrokeFromPopupEdit(data);
  }
  return result;
}

AnnotationEditorStrokePopupDeactivateTarget strokePopupDeactivateTarget(
    const AnnotationEditorHost* data, HWND activated)
{
  if (data == nullptr)
  {
    return AnnotationEditorStrokePopupDeactivateTarget::Outside;
  }
  if (activated == data->strokePopup().m_stroke_popup_edit)
  {
    return AnnotationEditorStrokePopupDeactivateTarget::ValueEdit;
  }
  if (activated == data->strokePopup().m_stroke_popup)
  {
    return AnnotationEditorStrokePopupDeactivateTarget::Popup;
  }
  return AnnotationEditorStrokePopupDeactivateTarget::Outside;
}

void applyEditorStrokeWidth(AnnotationEditorHost* data, int width)
{
  if (data == nullptr)
  {
    return;
  }

  const int clamped = annotationEditorClampStrokeWidthPx(width);
  data->core().m_controller.setStrokeWidth(static_cast<float>(clamped));
  if (data->core().m_controller.isDrawing())
  {
    invalidateImageArea(data);
  }
  invalidateToolbar(data);
  if (data->strokePopup().m_stroke_popup != nullptr &&
      IsWindowVisible(data->strokePopup().m_stroke_popup) != FALSE)
  {
    InvalidateRect(data->strokePopup().m_stroke_popup, nullptr, FALSE);
  }
}

bool handleStrokeChipWheel(AnnotationEditorHost* data, int delta)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsStroke(data->core().m_controller.tool()))
  {
    return false;
  }

  const int steps = annotationEditorWheelDeltaToSteps(delta);
  applyEditorStrokeWidth(
      data, annotationEditorStepStrokeWidth(currentStrokeWidthPx(data), steps));
  syncStrokePopupEdit(data);
  return true;
}

bool strokePopupIsVisible(const AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return false;
  }
  if (data->strokePopup().m_stroke_popup != nullptr &&
      IsWindowVisible(data->strokePopup().m_stroke_popup) != FALSE)
  {
    return true;
  }
  return data->strokePopup().m_stroke_popup_edit != nullptr &&
         IsWindowVisible(data->strokePopup().m_stroke_popup_edit) != FALSE;
}

void hideStrokePopupValueEdit(AnnotationEditorHost* data)
{
  if (data == nullptr || data->strokePopup().m_stroke_popup_edit == nullptr)
  {
    return;
  }
  if (ShowWindow(data->strokePopup().m_stroke_popup_edit, SW_HIDE) == 0)
  {
    // 已隐藏时返回 0，残留由 destroyStrokePopup 再收口。
  }
}

void positionStrokePopupValueEdit(AnnotationEditorHost* data)
{
  if (data == nullptr || data->strokePopup().m_stroke_popup == nullptr ||
      data->strokePopup().m_stroke_popup_edit == nullptr)
  {
    return;
  }

  RECT popup_rect{};
  if (GetWindowRect(data->strokePopup().m_stroke_popup, &popup_rect) == FALSE)
  {
    return;
  }

  int edit_x = 0;
  int edit_y = 0;
  annotationEditorStrokePopupValueScreenOrigin(popup_rect.left, popup_rect.top,
                                               edit_x, edit_y);
  const AnnotationEditorStrokePopupLayout layout =
      annotationEditorStrokePopupLayout();
  const RECT value = toWinRect(layout.value);
  if (SetWindowPos(data->strokePopup().m_stroke_popup_edit, HWND_TOPMOST, edit_x, edit_y,
                   value.right - value.left, value.bottom - value.top,
                   SWP_NOACTIVATE) == FALSE)
  {
    // 定位失败时数字框可能暂时错位，下次 show/hide 会再对齐。
  }
}

void hideStrokePopup(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->strokePopup().m_stroke_slider_dragging = false;
  // 数字框是独立 WS_POPUP，必须先于分层弹层隐藏，否则会留下白底粗细值。
  hideStrokePopupValueEdit(data);
  if (data->strokePopup().m_stroke_popup != nullptr)
  {
    if (ShowWindow(data->strokePopup().m_stroke_popup, SW_HIDE) == 0)
    {
      // 已隐藏时返回 0。
    }
  }
}

void destroyStrokePopup(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->strokePopup().m_stroke_slider_dragging = false;
  hideStrokePopupValueEdit(data);
  if (data->strokePopup().m_stroke_popup_edit != nullptr)
  {
    if (RemoveWindowSubclass(data->strokePopup().m_stroke_popup_edit,
                             strokePopupEditSubclassProc,
                             kStrokePopupEditSubclassId) == FALSE)
    {
      // 未装子类或已卸。
    }
    if (DestroyWindow(data->strokePopup().m_stroke_popup_edit) == FALSE)
    {
      // 窗口可能已随 owner 销毁。
    }
    data->strokePopup().m_stroke_popup_edit = nullptr;
  }
  if (data->strokePopup().m_stroke_popup != nullptr)
  {
    DestroyWindow(data->strokePopup().m_stroke_popup);
    data->strokePopup().m_stroke_popup = nullptr;
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

bool paintStrokePopup(HWND hwnd, const StrokePopupPaintSnapshot& snapshot)
{
  if (hwnd == nullptr)
  {
    return false;
  }

  RECT client{};
  GetClientRect(hwnd, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width <= 0 || height <= 0)
  {
    return false;
  }

  void* bits = nullptr;
  const HBITMAP dib = createTopDownArgbDib(width, height, &bits);
  if (dib == nullptr || bits == nullptr)
  {
    return false;
  }

  const HDC mem_dc = CreateCompatibleDC(nullptr);
  if (mem_dc == nullptr)
  {
    DeleteObject(dib);
    return false;
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
  RECT value_rect = toWinRect(layout.value);
  RECT hint = toWinRect(layout.hint);

  HFONT font = snapshot.combo_font;
  const HGDIOBJ old_font =
      (font != nullptr) ? SelectObject(mem_dc, font) : nullptr;
  SetBkMode(mem_dc, TRANSPARENT);
  SetTextColor(mem_dc, colors.label);
  DrawTextW(mem_dc, L"\x753B\x7B14", -1, &label,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  fillRoundRect(mem_dc, slider, kStrokeSliderTrackColor, kStrokeSliderTrackColor,
                slider.bottom - slider.top);

  const int width_px = snapshot.width_px;
  const int track_w =
      (std::max)(AnnotationEditorStrokeSliderMinExtentPx,
                 static_cast<int>(slider.right - slider.left));
  const int thumb_x =
      annotationEditorStrokeSliderX(width_px, slider.left, track_w);
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

  wchar_t value_text[AnnotationEditorStrokePopupValueTextMaxChars]{};
  if (annotationEditorStrokePopupFormatWidthText(
          width_px, value_text, AnnotationEditorStrokePopupValueTextMaxChars))
  {
    DrawTextW(mem_dc, value_text, -1, &value_rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  SetTextColor(mem_dc, kStrokePopupHintColor);
  DrawTextW(mem_dc, L"\x4E5F\x53EF\x4EE5\x7528\x6EDA\x8F6E\x8C03\x6574\x3002",
            -1, &hint, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  if (old_font != nullptr)
  {
    SelectObject(mem_dc, old_font);
  }

  promoteRgbToOpaqueAlpha(bits, width, height);
  const bool presented =
      presentLayeredArgbWindow(hwnd, mem_dc, width, height);
  SelectObject(mem_dc, old_bitmap);
  DeleteDC(mem_dc);
  DeleteObject(dib);
  return presented;
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
    {
      const PaintGuard paint(hwnd);
      if (data != nullptr)
      {
        (void)paintStrokePopup(
            hwnd,
            StrokePopupPaintSnapshot{data->chrome().m_combo_font.asFont(),
                                     currentStrokeWidthPx(data)});
      }
      return 0;
    }
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
        data->strokePopup().m_stroke_slider_dragging = true;
        SetCapture(hwnd);
        applyStrokeFromPopupSlider(data, x);
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      if (data != nullptr && data->strokePopup().m_stroke_slider_dragging)
      {
        const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
        applyStrokeFromPopupSlider(data, x);
      }
      return 0;
    case WM_LBUTTONUP:
      if (data != nullptr)
      {
        data->strokePopup().m_stroke_slider_dragging = false;
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
      if (data == nullptr)
      {
        return 0;
      }
      if (handleStrokePopupEditCommand(data, LOWORD(wparam), HIWORD(wparam),
                                       reinterpret_cast<HWND>(lparam)))
      {
        return 0;
      }
      return 0;
    }
    case WM_ACTIVATE:
      if (data != nullptr && LOWORD(wparam) == WA_INACTIVE)
      {
        const HWND activated = reinterpret_cast<HWND>(lparam);
        if (annotationEditorStrokePopupHidesOnDeactivate(
                strokePopupDeactivateTarget(data, activated)))
        {
          POINT pt{};
          GetCursorPos(&pt);
          ScreenToClient(data->window().m_overlay, &pt);
          if (!hitTestStrokeChip(data, pt.x, pt.y))
          {
            hideStrokePopup(data);
          }
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
  if (data == nullptr || data->window().m_overlay == nullptr ||
      !annotationEditorPropertyBarShowsStroke(data->core().m_controller.tool()))
  {
    return;
  }

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerStrokePopupClass(instance))
  {
    return;
  }

  RECT chip = data->chrome().m_stroke_chip_rect;
  POINT origin{chip.left, chip.bottom + AnnotationEditorButtonGap};
  ClientToScreen(data->window().m_overlay, &origin);

  if (data->strokePopup().m_stroke_popup == nullptr)
  {
    data->strokePopup().m_stroke_popup = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kStrokePopupClassName,
        L"", WS_POPUP | WS_CLIPCHILDREN, origin.x, origin.y,
        AnnotationEditorStrokePopupWidth, AnnotationEditorStrokePopupHeight,
        data->window().m_overlay, nullptr, instance, data);
    if (data->strokePopup().m_stroke_popup == nullptr)
    {
      return;
    }

    const AnnotationEditorStrokePopupLayout layout =
        annotationEditorStrokePopupLayout();
    const RECT value = toWinRect(layout.value);
    POINT edit_origin{value.left, value.top};
    if (ClientToScreen(data->strokePopup().m_stroke_popup, &edit_origin) == FALSE)
    {
      edit_origin.x = origin.x + value.left;
      edit_origin.y = origin.y + value.top;
    }
    data->strokePopup().m_stroke_popup_edit = CreateWindowExW(
        WS_EX_TOOLWINDOW, L"EDIT", L"",
        WS_POPUP | ES_NUMBER | ES_CENTER, edit_origin.x, edit_origin.y,
        value.right - value.left, value.bottom - value.top, data->strokePopup().m_stroke_popup,
        nullptr, instance, nullptr);
    if (data->strokePopup().m_stroke_popup_edit != nullptr)
    {
      if (SetWindowLongPtrW(data->strokePopup().m_stroke_popup_edit, GWLP_ID,
                            static_cast<LONG_PTR>(kStrokePopupEditId)) == 0)
      {
        // ID 仅用于 WM_COMMAND；HWND 比对仍可识别该编辑框。
      }
      if (SetWindowSubclass(data->strokePopup().m_stroke_popup_edit,
                            strokePopupEditSubclassProc,
                            kStrokePopupEditSubclassId,
                            reinterpret_cast<DWORD_PTR>(data)) == FALSE)
      {
        // 子类失败时仍走 owner/overlay 的 WM_COMMAND。
      }
    }
    if (data->strokePopup().m_stroke_popup_edit != nullptr && data->chrome().m_combo_font)
    {
      SendMessageW(data->strokePopup().m_stroke_popup_edit, WM_SETFONT,
                   reinterpret_cast<WPARAM>(data->chrome().m_combo_font.get()), TRUE);
    }
  }
  else
  {
    if (SetWindowPos(data->strokePopup().m_stroke_popup, HWND_TOPMOST, origin.x, origin.y,
                     AnnotationEditorStrokePopupWidth,
                     AnnotationEditorStrokePopupHeight, SWP_NOACTIVATE) == FALSE)
    {
      return;
    }
  }

  syncStrokePopupEdit(data);
  positionStrokePopupValueEdit(data);
  const StrokePopupPaintSnapshot snapshot{
      data->chrome().m_combo_font.asFont(), currentStrokeWidthPx(data)};
  if (!paintStrokePopup(data->strokePopup().m_stroke_popup, snapshot))
  {
    hideStrokePopup(data);
    return;
  }
  if (ShowWindow(data->strokePopup().m_stroke_popup, SW_SHOWNOACTIVATE) == 0)
  {
    // 先前已显示时返回 0；背景已由 UpdateLayeredWindow 显式呈现。
  }
  if (data->strokePopup().m_stroke_popup_edit != nullptr)
  {
    if (ShowWindow(data->strokePopup().m_stroke_popup_edit, SW_SHOW) == 0)
    {
      // 先前已显示时返回 0。
    }
  }
  SetForegroundWindow(data->strokePopup().m_stroke_popup);
  if (data->strokePopup().m_stroke_popup_edit != nullptr)
  {
    SetFocus(data->strokePopup().m_stroke_popup_edit);
    SendMessageW(data->strokePopup().m_stroke_popup_edit, EM_SETSEL, 0, -1);
  }
}
}  // namespace qingying
