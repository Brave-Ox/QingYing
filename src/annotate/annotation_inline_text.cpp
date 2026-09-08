#include "annotate/annotation_editor_host.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "annotate/annotation_editor_paint.h"

#include <commctrl.h>

#include "annotate/annotation_editor_chrome.h"
#include "annotate/annotation_editor_inline_text.h"
#include "annotate/annotation_text_rasterizer.h"

namespace qingying {

HBRUSH ensureInlineEditKeyBrush(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return nullptr;
  }
  if (!data->inlineText().m_inline_edit_key_brush)
  {
    data->inlineText().m_inline_edit_key_brush.reset(CreateSolidBrush(kToolbarColorKey));
  }
  return data->inlineText().m_inline_edit_key_brush.asBrush();
}

void destroyInlineEdit(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }

  data->inlineText().m_inline_edit_pressing = false;

  const HWND edit = data->inlineText().m_inline_edit;
  const HWND host = data->inlineText().m_inline_edit_host;
  // 必须先断开成员：DestroyWindow 会同步派发 EN_KILLFOCUS，
  // 否则 commitInlineText 会再次 add 同一条新文字。
  data->inlineText().m_inline_edit = nullptr;
  data->inlineText().m_inline_edit_host = nullptr;
  if (edit != nullptr && GetCapture() == edit)
  {
    ReleaseCapture();
  }
  if (edit != nullptr && data->inlineText().m_inline_edit_prev_proc != nullptr)
  {
    SetWindowLongPtrW(edit, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(data->inlineText().m_inline_edit_prev_proc));
    data->inlineText().m_inline_edit_prev_proc = nullptr;
  }
  if (host != nullptr)
  {
    DestroyWindow(host);
  }
  else if (edit != nullptr)
  {
    DestroyWindow(edit);
  }

  data->inlineText().m_inline_edit_font.reset();
  data->inlineText().m_inline_edit_key_brush.reset();
}

void fillTextHitBounds(HWND hwnd, Annotation& annotation)
{
  if (hwnd == nullptr || annotation.text.empty())
  {
    return;
  }

  const HDC hdc = GetDC(hwnd);
  if (hdc == nullptr)
  {
    return;
  }

  SIZE size{};
  if (measureAnnotationText(hdc, annotation, size))
  {
    annotation.bounds.x = annotation.start.x;
    annotation.bounds.y = annotation.start.y;
    annotation.bounds.width = static_cast<float>(
        (std::max)(static_cast<int>(size.cx), kTextMinHitWidthPx));
    annotation.bounds.height = static_cast<float>(
        (std::max)(static_cast<int>(size.cy), kTextMinHitHeightPx));
  }
  ReleaseDC(hwnd, hdc);
}

void commitInlineText(AnnotationEditorHost* data)
{
  if (data == nullptr || data->inlineText().m_inline_edit == nullptr)
  {
    return;
  }

  const AnnotationEditorInlineCommitGuard guard(data->inlineText().m_inline_commit_busy);
  if (!guard.acquired())
  {
    return;
  }

  wchar_t buffer[kInlineTextMaxChars]{};
  GetWindowTextW(data->inlineText().m_inline_edit, buffer, kInlineTextMaxChars);
  const float anchor_x = data->inlineText().m_text_anchor_x;
  const float anchor_y = data->inlineText().m_text_anchor_y;
  const std::size_t edit_index = data->inlineText().m_editing_text_index;
  HWND overlay = data->window().m_overlay;
  destroyInlineEdit(data);
  data->inlineText().m_editing_text_index = kInvalidAnnotationIndex;

  if (buffer[0] == L'\0')
  {
    return;
  }

  Annotation annotation;
  annotation.type = AnnotationType::Text;
  annotation.start = PointF{anchor_x, anchor_y};
  annotation.text.assign(buffer);
  annotation.style = data->core().m_controller.style();
  annotation.text_wrap_width = data->inlineText().m_text_wrap_width;
  annotation.rotation_degrees =
      data->inlineText().m_text_rotation_degrees;
  fillTextHitBounds(overlay, annotation);

  bool ok = false;
  if (edit_index != kInvalidAnnotationIndex)
  {
    ok = data->core().m_session.engine().replaceAt(edit_index, annotation);
  }
  else
  {
    ok = data->core().m_session.engine().add(annotation);
  }

  if (ok)
  {
    std::size_t index = edit_index;
    if (index == kInvalidAnnotationIndex)
    {
      const std::size_t count = data->core().m_session.engine().document().count();
      if (count > 0)
      {
        index = count - 1;
      }
    }
    if (overlay != nullptr && index != kInvalidAnnotationIndex)
    {
      selectTextAnnotation(data, overlay, index, 0, 0);
    }
    invalidateImageArea(data);
  }
}

void cancelInlineText(AnnotationEditorHost* data)
{
  destroyInlineEdit(data);
  if (data != nullptr)
  {
    data->inlineText().m_editing_text_index = kInvalidAnnotationIndex;
  }
}

void resetTextGesture(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->inlineText().m_text_gesture_active = false;
  data->inlineText().m_text_dragging = false;
  data->inlineText().m_text_rotating = false;
  data->inlineText().m_text_target_index = kInvalidAnnotationIndex;
}

void clearTextSelection(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->inlineText().m_selected_text_index = kInvalidAnnotationIndex;
  data->inlineText().m_last_text_click_index = kInvalidAnnotationIndex;
  data->inlineText().m_last_text_click_tick = 0;
}

void selectTextAnnotation(AnnotationEditorHost* data, HWND hwnd, std::size_t index,
                          int click_x, int click_y)
{
  if (data == nullptr || hwnd == nullptr)
  {
    return;
  }
  data->inlineText().m_selected_text_index = index;
  data->inlineText().m_last_text_click_index = index;
  data->inlineText().m_last_text_click_tick = GetTickCount();
  data->inlineText().m_last_text_click_x = click_x;
  data->inlineText().m_last_text_click_y = click_y;
  data->core().m_controller.setTool(AnnotationTool::Text);
  resizeEditorChrome(data);
  if (index < data->core().m_session.engine().document().count())
  {
    syncStyleFromAnnotation(
        data, data->core().m_session.engine().document().items().at(index));
  }
  SetFocus(hwnd);
  invalidateImageArea(data);
}

bool isTextDoubleClick(const AnnotationEditorHost* data, std::size_t hit, int x,
                       int y)
{
  if (data == nullptr || hit == kInvalidAnnotationIndex ||
      data->inlineText().m_last_text_click_index != hit)
  {
    return false;
  }

  const DWORD elapsed = GetTickCount() - data->inlineText().m_last_text_click_tick;
  if (elapsed > GetDoubleClickTime())
  {
    return false;
  }

  const int limit_x = GetSystemMetrics(SM_CXDOUBLECLK) / 2;
  const int limit_y = GetSystemMetrics(SM_CYDOUBLECLK) / 2;
  const int dx = x - data->inlineText().m_last_text_click_x;
  const int dy = y - data->inlineText().m_last_text_click_y;
  return dx >= -limit_x && dx <= limit_x && dy >= -limit_y && dy <= limit_y;
}

bool deleteSelectedTextAnnotation(AnnotationEditorHost* data)
{
  if (data == nullptr || data->inlineText().m_inline_edit != nullptr)
  {
    return false;
  }
  if (data->inlineText().m_selected_text_index == kInvalidAnnotationIndex ||
      data->inlineText().m_selected_text_index >= data->core().m_session.engine().document().count())
  {
    return false;
  }

  const std::size_t index = data->inlineText().m_selected_text_index;
  if (!data->core().m_session.engine().removeAt(index))
  {
    return false;
  }

  clearTextSelection(data);
  resetTextGesture(data);
  invalidateImageArea(data);
  return true;
}

void selectFontSizeInCombo(AnnotationEditorHost* data, int font_size)
{
  if (data == nullptr)
  {
    return;
  }

  data->core().m_controller.setFontSize(font_size);
  if (data->chrome().m_font_combo == nullptr)
  {
    return;
  }

  data->chrome().m_size_combo_syncing = true;
  for (int i = 0; i < AnnotationEditorFontSizeOptionCount; ++i)
  {
    if (AnnotationEditorFontSizeOptions[i] == font_size)
    {
      SendMessageW(data->chrome().m_font_combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
      data->chrome().m_size_combo_syncing = false;
      return;
    }
  }
  data->chrome().m_size_combo_syncing = false;
}

bool measureAnnotationText(HDC hdc, const Annotation& annotation, SIZE& out_size)
{
  constexpr int LegacySingleLineWidth = 32767;
  const int width = annotation.text_wrap_width > 0.0f
                        ? static_cast<int>(annotation.text_wrap_width)
                        : LegacySingleLineWidth;
  return measureAnnotationTextLayout(hdc, annotation, width, out_size);
}

void measureTextHitSize(HDC hdc, const Annotation& annotation, int& out_width,
                        int& out_height)
{
  out_width = (std::max)(static_cast<int>(annotation.bounds.width),
                         kTextMinHitWidthPx);
  out_height = (std::max)(static_cast<int>(annotation.bounds.height),
                          kTextMinHitHeightPx);
  SIZE size{};
  if (measureAnnotationText(hdc, annotation, size))
  {
    const int layout_width =
        annotation.text_wrap_width > 0.0f
            ? static_cast<int>(annotation.text_wrap_width)
            : static_cast<int>(size.cx);
    out_width = (std::max)(layout_width, kTextMinHitWidthPx);
    out_height = (std::max)(static_cast<int>(size.cy), kTextMinHitHeightPx);
  }
}

AnnotationEditorTextChrome makeTextChrome(const AnnotationEditorHost* data,
                                          const Annotation& annotation,
                                          bool use_drag_position)
{
  AnnotationEditorTextChrome chrome{};
  if (data == nullptr)
  {
    return chrome;
  }

  int width = kTextMinHitWidthPx;
  int height = kTextMinHitHeightPx;
  const HDC hdc = GetDC(data->window().m_overlay);
  if (hdc != nullptr)
  {
    measureTextHitSize(hdc, annotation, width, height);
    ReleaseDC(data->window().m_overlay, hdc);
  }

  int text_x = static_cast<int>(annotation.start.x);
  int text_y = static_cast<int>(annotation.start.y);
  if (use_drag_position)
  {
    text_x = static_cast<int>(data->inlineText().m_text_drag_x);
    text_y = static_cast<int>(data->inlineText().m_text_drag_y);
  }
  float rotation_degrees = annotation.rotation_degrees;
  if (data->inlineText().m_text_rotating &&
      data->inlineText().m_text_target_index ==
          data->inlineText().m_selected_text_index)
  {
    rotation_degrees = data->inlineText().m_text_rotation_degrees;
  }
  return annotationEditorTextChrome(
      data->window().m_image_origin_x, data->window().m_image_origin_y, text_x,
      text_y, width, height, rotation_degrees);
}

AnnotationEditorTextChrome makeTextChrome(
    const AnnotationEditorPaintSnapshot& snapshot,
    const Annotation& annotation, bool use_drag_position)
{
  AnnotationEditorTextChrome chrome{};
  int width = kTextMinHitWidthPx;
  int height = kTextMinHitHeightPx;
  const HDC hdc = GetDC(snapshot.overlay);
  if (hdc != nullptr)
  {
    measureTextHitSize(hdc, annotation, width, height);
    ReleaseDC(snapshot.overlay, hdc);
  }

  int text_x = static_cast<int>(annotation.start.x);
  int text_y = static_cast<int>(annotation.start.y);
  if (use_drag_position)
  {
    text_x = static_cast<int>(snapshot.text_drag_x);
    text_y = static_cast<int>(snapshot.text_drag_y);
  }
  float rotation_degrees = annotation.rotation_degrees;
  if (snapshot.text_rotating &&
      snapshot.text_target_index == snapshot.selected_text_index)
  {
    rotation_degrees = snapshot.text_rotation_degrees;
  }
  return annotationEditorTextChrome(
      snapshot.image_origin_x, snapshot.image_origin_y, text_x, text_y, width,
      height, rotation_degrees);
}

std::size_t hitTestTextAnnotation(AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr || data->window().m_overlay == nullptr)
  {
    return kInvalidAnnotationIndex;
  }

  const auto& items = data->core().m_session.engine().document().items();
  for (std::size_t i = items.size(); i > 0; --i)
  {
    const std::size_t index = i - 1;
    const Annotation& annotation = items.at(index);
    if (annotation.type != AnnotationType::Text)
    {
      continue;
    }

    const bool dragging =
        data->inlineText().m_text_dragging && data->inlineText().m_text_target_index == index;
    const AnnotationEditorTextChrome chrome =
        makeTextChrome(data, annotation, dragging);
    const AnnotationEditorTextHit chrome_hit =
        annotationEditorHitTextChrome(chrome, x, y);
    if (chrome_hit != AnnotationEditorTextHit::None &&
        (chrome_hit != AnnotationEditorTextHit::Rotate ||
         data->inlineText().m_selected_text_index == index))
    {
      return index;
    }

    const PointF local = annotationEditorInverseRotatePoint(
        PointF{static_cast<float>(x), static_cast<float>(y)}, chrome.center,
        chrome.rotation_degrees);
    AnnotationEditorRect grab = chrome.frame;
    grab.left -= kTextHitPaddingPx;
    grab.top -= kTextHitPaddingPx;
    grab.right += kTextHitPaddingPx;
    grab.bottom += kTextHitPaddingPx;
    if (local.x >= static_cast<float>(grab.left) &&
        local.x < static_cast<float>(grab.right) &&
        local.y >= static_cast<float>(grab.top) &&
        local.y < static_cast<float>(grab.bottom))
    {
      return index;
    }
  }
  return kInvalidAnnotationIndex;
}

void tryPromoteInlineEditToDrag(AnnotationEditorHost* data, int client_x,
                                int client_y)
{
  if (data == nullptr || data->inlineText().m_inline_edit == nullptr ||
      data->inlineText().m_editing_text_index == kInvalidAnnotationIndex ||
      data->inlineText().m_editing_text_index >= data->core().m_session.engine().document().count())
  {
    return;
  }

  const int dx = client_x - data->inlineText().m_inline_edit_press_x;
  const int dy = client_y - data->inlineText().m_inline_edit_press_y;
  if (dx * dx + dy * dy < kTextDragThresholdPx * kTextDragThresholdPx)
  {
    return;
  }

  // 先把编辑框里的文案写回文档，再进入拖拽，避免未提交内容丢失。
  wchar_t buffer[kInlineTextMaxChars]{};
  GetWindowTextW(data->inlineText().m_inline_edit, buffer, kInlineTextMaxChars);
  const std::size_t index = data->inlineText().m_editing_text_index;
  Annotation updated =
      data->core().m_session.engine().document().items().at(index);
  if (buffer[0] != L'\0')
  {
    updated.text.assign(buffer);
    updated.style = data->core().m_controller.style();
    fillTextHitBounds(data->window().m_overlay, updated);
    (void)data->core().m_session.engine().replaceAt(index, updated);
  }

  POINT press{data->inlineText().m_inline_edit_press_x, data->inlineText().m_inline_edit_press_y};
  ClientToScreen(data->inlineText().m_inline_edit, &press);
  ScreenToClient(data->window().m_overlay, &press);

  POINT current{client_x, client_y};
  ClientToScreen(data->inlineText().m_inline_edit, &current);
  ScreenToClient(data->window().m_overlay, &current);

  destroyInlineEdit(data);
  data->inlineText().m_editing_text_index = kInvalidAnnotationIndex;

  data->inlineText().m_text_gesture_active = true;
  data->inlineText().m_text_dragging = true;
  data->inlineText().m_text_target_index = index;
  data->inlineText().m_text_press_x = static_cast<float>(press.x);
  data->inlineText().m_text_press_y = static_cast<float>(press.y);
  data->inlineText().m_text_origin_x = updated.start.x;
  data->inlineText().m_text_origin_y = updated.start.y;
  data->inlineText().m_text_drag_x =
      updated.start.x + static_cast<float>(current.x - press.x);
  data->inlineText().m_text_drag_y =
      updated.start.y + static_cast<float>(current.y - press.y);
  SetCapture(data->window().m_overlay);
  invalidateImageArea(data);
}

bool isCtrlZKey(WPARAM key) {
  return key == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

float textRotationPointerDegrees(const PointF& center, int x, int y)
{
  const double radians =
      std::atan2(static_cast<double>(y) - center.y,
                 static_cast<double>(x) - center.x);
  return static_cast<float>(radians * 180.0 / AnnotationEditorPi);
}

LRESULT CALLBACK inlineEditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                        LPARAM lparam)
{
  AnnotationEditorHost* data = reinterpret_cast<AnnotationEditorHost*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  const WNDPROC prev =
      data != nullptr ? data->inlineText().m_inline_edit_prev_proc : nullptr;

  if (data != nullptr)
  {
    if (msg == WM_LBUTTONDOWN)
    {
      data->inlineText().m_inline_edit_pressing = true;
      data->inlineText().m_inline_edit_press_x =
          static_cast<int>(static_cast<short>(LOWORD(lparam)));
      data->inlineText().m_inline_edit_press_y =
          static_cast<int>(static_cast<short>(HIWORD(lparam)));
      // 必须捕获：拖出 EDIT 客户区后否则收不到 MOVE，无法晋升为标注拖拽。
      SetCapture(hwnd);
    }
    else if (msg == WM_MOUSEMOVE && data->inlineText().m_inline_edit_pressing &&
             (wparam & MK_LBUTTON) != 0)
    {
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      tryPromoteInlineEditToDrag(data, x, y);
      if (data->inlineText().m_text_dragging)
      {
        return 0;
      }
    }
    else if (msg == WM_LBUTTONUP)
    {
      data->inlineText().m_inline_edit_pressing = false;
      if (GetCapture() == hwnd)
      {
        ReleaseCapture();
      }
    }
    else if (msg == WM_ERASEBKGND)
    {
      const HDC hdc = reinterpret_cast<HDC>(wparam);
      RECT client{};
      GetClientRect(hwnd, &client);
      const HBRUSH brush = ensureInlineEditKeyBrush(data);
      if (hdc != nullptr && brush != nullptr)
      {
        FillRect(hdc, &client, brush);
      }
      return 1;
    }
    else if (msg == WM_MOUSEWHEEL)
    {
      if (data->window().m_overlay != nullptr)
      {
        (void)SendMessageW(data->window().m_overlay, WM_MOUSEWHEEL, wparam,
                           lparam);
      }
      return 0;
    }
    else if (msg == WM_KEYDOWN)
    {
      if (wparam == VK_RETURN)
      {
        commitInlineText(data);
        return 0;
      }
      if (wparam == VK_ESCAPE)
      {
        (void)handleEditorKeyDown(data->window().m_overlay, data, wparam);
        return 0;
      }
      if (isCtrlZKey(wparam))
      {
        cancelInlineText(data);
        clearTextSelection(data);
        if (data->core().m_controller.undo(data->core().m_session.engine()))
        {
          invalidateImageArea(data);
        }
        return 0;
      }
    }
  }

  if (prev != nullptr)
  {
    const LRESULT result = CallWindowProcW(prev, hwnd, msg, wparam, lparam);
    if (msg == WM_SETFOCUS || msg == WM_PAINT)
    {
      HideCaret(hwnd);
    }
    return result;
  }
  if (msg == WM_SETFOCUS)
  {
    HideCaret(hwnd);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void invalidateInlineEditRegion(const AnnotationEditorHost* data)
{
  if (data == nullptr || data->window().m_overlay == nullptr ||
      data->inlineText().m_inline_edit == nullptr)
  {
    return;
  }

  RECT rect{};
  if (GetWindowRect(data->inlineText().m_inline_edit, &rect) == FALSE)
  {
    return;
  }
  MapWindowPoints(HWND_DESKTOP, data->window().m_overlay, reinterpret_cast<POINT*>(&rect),
                  2);
  const int pad = AnnotationEditorInlineEditBorderPx + 1;
  (void)InflateRect(&rect, pad, pad);
  InvalidateRect(data->window().m_overlay, &rect, FALSE);
}

void placeInlineEditCaret(HWND edit, int caret, bool scroll_to_caret)
{
  if (edit == nullptr)
  {
    return;
  }
  const int pos = (std::max)(0, caret);
  const LONG_PTR style = GetWindowLongPtrW(edit, GWL_STYLE);
  if (!scroll_to_caret)
  {
    // 暂时关掉 AUTOHSCROLL，避免 SetSel(末尾) 把刚复位的起点再滚走。
    (void)SetWindowLongPtrW(edit, GWL_STYLE,
                            style & ~static_cast<LONG_PTR>(ES_AUTOHSCROLL));
    (void)SendMessageW(edit, EM_SETSEL, 0, 0);
    (void)SendMessageW(edit, EM_SETSEL, pos, pos);
    (void)SetWindowLongPtrW(edit, GWL_STYLE, style);
    return;
  }
  (void)SendMessageW(edit, EM_SETSEL, pos, pos);
  (void)SendMessageW(edit, EM_SCROLLCARET, 0, 0);
}

void layoutInlineEdit(AnnotationEditorHost* data)
{
  if (data == nullptr || data->inlineText().m_inline_edit == nullptr)
  {
    return;
  }

  const int canvas_x = static_cast<int>(data->inlineText().m_text_anchor_x);
  const int canvas_y = static_cast<int>(data->inlineText().m_text_anchor_y);

  wchar_t buffer[kInlineTextMaxChars]{};
  GetWindowTextW(data->inlineText().m_inline_edit, buffer, kInlineTextMaxChars);

  int text_extent = 0;
  int width = annotationEditorInlineEditWidth(
      text_extent, static_cast<int>(data->inlineText().m_text_max_width));
  SIZE layout_size{};
  const HDC hdc = GetDC(data->inlineText().m_inline_edit);
  if (hdc != nullptr)
  {
    HGDIOBJ old_font = nullptr;
    if (data->inlineText().m_inline_edit_font)
    {
      old_font = SelectObject(hdc, data->inlineText().m_inline_edit_font.get());
    }
    const int len = lstrlenW(buffer);
    SIZE size{};
    if (len > 0)
    {
      (void)GetTextExtentPoint32W(hdc, buffer, len, &size);
      text_extent = static_cast<int>(size.cx);
      ABC first{};
      ABC last{};
      if (GetCharABCWidthsW(hdc, static_cast<UINT>(buffer[0]),
                            static_cast<UINT>(buffer[0]), &first) != FALSE &&
          first.abcA < 0)
      {
        text_extent += -first.abcA;
      }
      if (GetCharABCWidthsW(hdc, static_cast<UINT>(buffer[len - 1]),
                            static_cast<UINT>(buffer[len - 1]), &last) !=
              FALSE &&
          last.abcC < 0)
      {
        text_extent += -last.abcC;
      }
    }
    width = annotationEditorInlineEditWidth(
        text_extent, static_cast<int>(data->inlineText().m_text_max_width));
    Annotation draft;
    draft.type = AnnotationType::Text;
    draft.style = data->core().m_controller.style();
    draft.text.assign(buffer);
    draft.text_wrap_width = static_cast<float>(width);
    (void)measureAnnotationTextLayout(hdc, draft, width, layout_size);
    if (old_font != nullptr)
    {
      SelectObject(hdc, old_font);
    }
    ReleaseDC(data->inlineText().m_inline_edit, hdc);
  }

  invalidateInlineEditRegion(data);
  data->inlineText().m_text_wrap_width = static_cast<float>(width);
  const int font_size = data->core().m_controller.style().font_size;
  const int layout_height =
      layout_size.cy > 0 ? static_cast<int>(layout_size.cy) : font_size;
  const int height = layout_height + AnnotationEditorInlineEditHeightPad;
  const HWND host = data->inlineText().m_inline_edit_host != nullptr ? data->inlineText().m_inline_edit_host
                                                      : data->inlineText().m_inline_edit;
  positionOwnedPopup(host, data->window().m_overlay, data->window().m_image_origin_x + canvas_x,
                     data->window().m_image_origin_y + canvas_y, width, height);
  if (data->inlineText().m_inline_edit_host != nullptr)
  {
    SetWindowPos(data->inlineText().m_inline_edit, nullptr, 0, 0, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  placeInlineEditCaret(data->inlineText().m_inline_edit, lstrlenW(buffer),
                       false);
  invalidateInlineEditRegion(data);
  invalidateImageArea(data);
}

void applyInlineEditVisual(AnnotationEditorHost* data)
{
  if (data == nullptr || data->inlineText().m_inline_edit == nullptr)
  {
    return;
  }
  InvalidateRect(data->inlineText().m_inline_edit, nullptr, TRUE);
  layoutInlineEdit(data);
}

void paintLiveInlineText(HDC hdc,
                         const AnnotationEditorPaintSnapshot& snapshot)
{
  if (hdc == nullptr || !snapshot.inline_edit_visible)
  {
    return;
  }

  const int text_len = static_cast<int>(snapshot.inline_text.size());
  const int font_px =
      (std::min)((std::max)(snapshot.style.font_size, MinFontSize),
                 MaxFontSize);
  const GdiObject font = createAnnotationTextFont(snapshot.style, font_px);
  if (!font)
  {
    return;
  }

  const SelectGuard selected_font(hdc, font.get());
  const COLORREF color = colorBgraToRef(snapshot.style.color);
  SetTextColor(hdc, color);
  SetBkMode(hdc, TRANSPARENT);

  const int origin_x =
      snapshot.image_origin_x + static_cast<int>(snapshot.text_anchor_x);
  const int origin_y =
      snapshot.image_origin_y + static_cast<int>(snapshot.text_anchor_y);
  const int wrap_width =
      (std::max)(1, static_cast<int>(snapshot.text_wrap_width));
  const int edit_height = static_cast<int>(
      snapshot.inline_edit_rect.bottom - snapshot.inline_edit_rect.top);
  const int layout_height = (std::max)(
      font_px, edit_height - AnnotationEditorInlineEditHeightPad);
  const int saved_dc = SaveDC(hdc);
  if (saved_dc == 0)
  {
    return;
  }
  const float center_x =
      static_cast<float>(origin_x) + static_cast<float>(wrap_width) / 2.0f;
  const float center_y =
      static_cast<float>(origin_y) + static_cast<float>(layout_height) / 2.0f;
  if (!applyAnnotationTextWorldTransform(
          hdc, snapshot.text_rotation_degrees, center_x, center_y))
  {
    (void)RestoreDC(hdc, saved_dc);
    return;
  }
  if (text_len > 0)
  {
    RECT text_rect{origin_x, origin_y, origin_x + wrap_width,
                   origin_y + snapshot.source.height};
    (void)DrawTextW(hdc, snapshot.inline_text.c_str(), text_len, &text_rect,
                    DT_LEFT | DT_TOP | DT_NOPREFIX | DT_WORDBREAK |
                        DT_EDITCONTROL);
  }

  int caret = snapshot.inline_caret;
  if (caret < 0)
  {
    caret = 0;
  }
  if (caret > text_len)
  {
    caret = text_len;
  }

  const int caret_x =
      snapshot.inline_edit_rect.left + snapshot.inline_caret_position.x;
  const int caret_y =
      snapshot.inline_edit_rect.top + snapshot.inline_caret_position.y;
  const GdiObject pen(CreatePen(PS_SOLID, kInlineCaretWidthPx, color));
  if (pen)
  {
    const SelectGuard selected_pen(hdc, pen.get());
    MoveToEx(hdc, caret_x, caret_y, nullptr);
    LineTo(hdc, caret_x, caret_y + font_px);
  }
  (void)RestoreDC(hdc, saved_dc);
}

void paintInlineEditFrame(HDC hdc,
                          const AnnotationEditorPaintSnapshot& snapshot)
{
  if (hdc == nullptr || !snapshot.inline_edit_visible ||
      IsRectEmpty(&snapshot.inline_edit_rect) != FALSE)
  {
    return;
  }

  const RECT& rect = snapshot.inline_edit_rect;
  const int border = AnnotationEditorInlineEditBorderPx;
  const GdiObject pen(CreatePen(PS_SOLID, border, kInlineEditBorderColor));
  if (!pen)
  {
    return;
  }
  const SelectGuard selected_pen(hdc, pen.get());
  const SelectGuard selected_brush(hdc, GetStockObject(NULL_BRUSH));
  const int edit_layout_height = static_cast<int>(
      rect.bottom - rect.top - AnnotationEditorInlineEditHeightPad);
  const float layout_height =
      static_cast<float>((std::max)(1, edit_layout_height));
  const PointF center{
      static_cast<float>(rect.left) + snapshot.text_wrap_width / 2.0f,
      static_cast<float>(rect.top) + layout_height / 2.0f};
  const PointF frame_points[4]{
      PointF{static_cast<float>(rect.left - border),
             static_cast<float>(rect.top - border)},
      PointF{static_cast<float>(rect.right + border),
             static_cast<float>(rect.top - border)},
      PointF{static_cast<float>(rect.right + border),
             static_cast<float>(rect.bottom + border)},
      PointF{static_cast<float>(rect.left - border),
             static_cast<float>(rect.bottom + border)}};
  POINT rotated[5]{};
  for (int index = 0; index < 4; ++index)
  {
    const PointF point = annotationEditorRotatePoint(
        frame_points[index], center, snapshot.text_rotation_degrees);
    rotated[index].x = static_cast<LONG>(std::lround(point.x));
    rotated[index].y = static_cast<LONG>(std::lround(point.y));
  }
  rotated[4] = rotated[0];
  (void)Polyline(hdc, rotated, 5);
}

LRESULT CALLBACK inlineEditHostWndProc(HWND hwnd, UINT msg, WPARAM wparam,
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
    case WM_ERASEBKGND:
    {
      const HDC hdc = reinterpret_cast<HDC>(wparam);
      RECT client{};
      GetClientRect(hwnd, &client);
      const HBRUSH brush = ensureInlineEditKeyBrush(data);
      if (hdc != nullptr && brush != nullptr)
      {
        FillRect(hdc, &client, brush);
      }
      return 1;
    }
    case WM_CTLCOLOREDIT:
    {
      if (data == nullptr || data->inlineText().m_inline_edit == nullptr ||
          reinterpret_cast<HWND>(lparam) != data->inlineText().m_inline_edit)
      {
        break;
      }
      const HDC hdc = reinterpret_cast<HDC>(wparam);
      SetTextColor(hdc, kToolbarColorKey);
      SetBkColor(hdc, kToolbarColorKey);
      const HBRUSH brush = ensureInlineEditKeyBrush(data);
      if (brush != nullptr)
      {
        return reinterpret_cast<LRESULT>(brush);
      }
      break;
    }
    case WM_MOUSEWHEEL:
      if (data != nullptr)
      {
        if (data->window().m_overlay != nullptr)
        {
          return SendMessageW(data->window().m_overlay, WM_MOUSEWHEEL, wparam,
                              lparam);
        }
      }
      return 0;
    case WM_COMMAND:
      if (data != nullptr && data->window().m_overlay != nullptr)
      {
        return SendMessageW(data->window().m_overlay, WM_COMMAND, wparam, lparam);
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool registerInlineEditHostClass(HINSTANCE instance)
{
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = inlineEditHostWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
  wc.lpszClassName = kInlineEditHostClassName;
  return RegisterClassExW(&wc) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void beginInlineText(AnnotationEditorHost* data, HWND hwnd, int x, int y,
                     std::size_t edit_index)
{
  if (data == nullptr)
  {
    return;
  }

  commitInlineText(data);
  resetTextGesture(data);
  if (edit_index == kInvalidAnnotationIndex)
  {
    clearTextSelection(data);
  }
  else
  {
    data->inlineText().m_selected_text_index = edit_index;
  }

  const Image& source = data->core().m_session.source();
  std::wstring initial_text;
  data->inlineText().m_editing_text_index = edit_index;
  float click_x = 0.0f;
  float click_y = 0.0f;
  canvasFromClient(data, x, y, click_x, click_y);
  data->inlineText().m_text_anchor_x = click_x;
  data->inlineText().m_text_anchor_y = click_y;
  data->inlineText().m_text_rotation_degrees = 0.0f;
  data->inlineText().m_text_max_width = static_cast<float>(
      annotationEditorTextAvailableWidth(source.width, click_x));
  data->inlineText().m_text_wrap_width =
      static_cast<float>(annotationEditorInlineEditWidth(
          0, static_cast<int>(data->inlineText().m_text_max_width)));

  if (edit_index != kInvalidAnnotationIndex &&
      edit_index < data->core().m_session.engine().document().count())
  {
    const Annotation& existing =
        data->core().m_session.engine().document().items().at(edit_index);
    data->inlineText().m_text_anchor_x = existing.start.x;
    data->inlineText().m_text_anchor_y = existing.start.y;
    data->inlineText().m_text_rotation_degrees =
        existing.rotation_degrees;
    const float available_width = static_cast<float>(
        annotationEditorTextAvailableWidth(source.width, existing.start.x));
    data->inlineText().m_text_max_width =
        existing.text_wrap_width > 0.0f
            ? (std::min)(existing.text_wrap_width, available_width)
            : available_width;
    initial_text = existing.text;
    selectFontSizeInCombo(data, existing.style.font_size);
    data->core().m_controller.setColor(existing.style.color);
  }

  const int canvas_x = static_cast<int>(data->inlineText().m_text_anchor_x);
  const int canvas_y = static_cast<int>(data->inlineText().m_text_anchor_y);
  const int remain_width = source.width - canvas_x;
  const int edit_width = annotationEditorInlineEditWidth(0, remain_width);
  const int edit_height =
      annotationEditorInlineEditHeight(data->core().m_controller.style().font_size);

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerInlineEditHostClass(instance) ||
      ensureInlineEditKeyBrush(data) == nullptr)
  {
    data->inlineText().m_editing_text_index = kInvalidAnnotationIndex;
    return;
  }

  // 分层宿主打孔透出截图；子 EDIT 继续承接 IME。系统 EDIT 不能直接做 overlay 子窗。
  data->inlineText().m_inline_edit_host = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kInlineEditHostClassName,
      L"", WS_POPUP | WS_CLIPCHILDREN, 0, 0, edit_width, edit_height, hwnd,
      nullptr, instance, data);
  if (data->inlineText().m_inline_edit_host == nullptr)
  {
    data->inlineText().m_editing_text_index = kInvalidAnnotationIndex;
    return;
  }
  applyToolbarColorKey(data->inlineText().m_inline_edit_host);

  // 多行 EDIT 在达到截图右边界后自动换行；透明子窗只承接键盘和 IME，
  // 实际字形由 Overlay 用相同排版参数绘制。
  data->inlineText().m_inline_edit = CreateWindowExW(
      0, L"EDIT", initial_text.c_str(),
      WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_LEFT, 0, 0,
      edit_width, edit_height, data->inlineText().m_inline_edit_host,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kInlineEditId)),
      instance, nullptr);
  if (data->inlineText().m_inline_edit == nullptr)
  {
    DestroyWindow(data->inlineText().m_inline_edit_host);
    data->inlineText().m_inline_edit_host = nullptr;
    data->inlineText().m_editing_text_index = kInvalidAnnotationIndex;
    return;
  }
  SendMessageW(data->inlineText().m_inline_edit, EM_SETLIMITTEXT, kInlineTextMaxChars - 1, 0);
  SendMessageW(data->inlineText().m_inline_edit, EM_SETMARGINS,
               EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
  const AnnotationStyle& style = data->core().m_controller.style();
  data->inlineText().m_inline_edit_font =
      createAnnotationTextFont(style, style.font_size);
  if (data->inlineText().m_inline_edit_font)
  {
    SendMessageW(data->inlineText().m_inline_edit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(data->inlineText().m_inline_edit_font.get()), TRUE);
  }

  SetWindowLongPtrW(data->inlineText().m_inline_edit, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(data));
  data->inlineText().m_inline_edit_prev_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
      data->inlineText().m_inline_edit, GWLP_WNDPROC,
      reinterpret_cast<LONG_PTR>(inlineEditSubclassProc)));
  const int text_len = static_cast<int>(initial_text.size());
  SendMessageW(data->inlineText().m_inline_edit, EM_SETSEL, text_len, text_len);
  layoutInlineEdit(data);
  ShowWindow(data->inlineText().m_inline_edit_host, SW_SHOWNOACTIVATE);
  SetFocus(data->inlineText().m_inline_edit);
  invalidateImageArea(data);
}

void beginOrEditTextAt(AnnotationEditorHost* data, HWND hwnd, int x, int y)
{
  if (data == nullptr)
  {
    return;
  }

  // 先收起当前输入框，避免子 EDIT 抢走后续拖拽鼠标消息。
  commitInlineText(data);

  const std::size_t hit = hitTestTextAnnotation(data, x, y);
  if (hit != kInvalidAnnotationIndex)
  {
    const Annotation& existing =
        data->core().m_session.engine().document().items().at(hit);
    const AnnotationEditorTextChrome chrome =
        makeTextChrome(data, existing, false);
    const AnnotationEditorTextHit chrome_hit =
        annotationEditorHitTextChrome(chrome, x, y);
    if (chrome_hit == AnnotationEditorTextHit::Delete)
    {
      selectTextAnnotation(data, hwnd, hit, x, y);
      (void)deleteSelectedTextAnnotation(data);
      return;
    }
    if (chrome_hit == AnnotationEditorTextHit::Rotate)
    {
      selectTextAnnotation(data, hwnd, hit, x, y);
      data->inlineText().m_text_gesture_active = true;
      data->inlineText().m_text_rotating = true;
      data->inlineText().m_text_target_index = hit;
      data->inlineText().m_text_rotation_initial_degrees =
          existing.rotation_degrees;
      data->inlineText().m_text_rotation_degrees =
          existing.rotation_degrees;
      data->inlineText().m_text_rotation_press_degrees =
          textRotationPointerDegrees(chrome.center, x, y);
      SetCapture(hwnd);
      return;
    }
    if (isTextDoubleClick(data, hit, x, y))
    {
      data->inlineText().m_last_text_click_index = kInvalidAnnotationIndex;
      beginInlineText(data, hwnd, x, y, hit);
      return;
    }

    selectTextAnnotation(data, hwnd, hit, x, y);
    data->inlineText().m_text_gesture_active = true;
    data->inlineText().m_text_dragging = false;
    data->inlineText().m_text_target_index = hit;
    data->inlineText().m_text_press_x = static_cast<float>(x);
    data->inlineText().m_text_press_y = static_cast<float>(y);
    data->inlineText().m_text_origin_x = existing.start.x;
    data->inlineText().m_text_origin_y = existing.start.y;
    data->inlineText().m_text_drag_x = existing.start.x;
    data->inlineText().m_text_drag_y = existing.start.y;
    SetCapture(hwnd);
    return;
  }

  clearTextSelection(data);
  beginInlineText(data, hwnd, x, y, kInvalidAnnotationIndex);
}

void updateTextGesture(AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr || !data->inlineText().m_text_gesture_active)
  {
    return;
  }

  if (data->inlineText().m_text_rotating)
  {
    const std::size_t index = data->inlineText().m_text_target_index;
    if (index == kInvalidAnnotationIndex ||
        index >= data->core().m_session.engine().document().count())
    {
      return;
    }
    const Annotation& annotation =
        data->core().m_session.engine().document().items().at(index);
    const AnnotationEditorTextChrome chrome =
        makeTextChrome(data, annotation, false);
    const float pointer_degrees =
        textRotationPointerDegrees(chrome.center, x, y);
    const float delta =
        pointer_degrees -
        data->inlineText().m_text_rotation_press_degrees;
    const bool snap = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    data->inlineText().m_text_rotation_degrees =
        annotationEditorSnapRotationDegrees(
            data->inlineText().m_text_rotation_initial_degrees + delta, snap);
    invalidateImageArea(data);
    return;
  }

  const float dx = static_cast<float>(x) - data->inlineText().m_text_press_x;
  const float dy = static_cast<float>(y) - data->inlineText().m_text_press_y;
  if (!data->inlineText().m_text_dragging)
  {
    const float distance_sq = dx * dx + dy * dy;
    const float threshold = static_cast<float>(kTextDragThresholdPx *
                                               kTextDragThresholdPx);
    if (distance_sq < threshold)
    {
      return;
    }
    data->inlineText().m_text_dragging = true;
  }

  data->inlineText().m_text_drag_x = data->inlineText().m_text_origin_x + dx;
  data->inlineText().m_text_drag_y = data->inlineText().m_text_origin_y + dy;
  invalidateImageArea(data);
}

void finishTextGesture(AnnotationEditorHost* data, HWND hwnd, int x, int y)
{
  if (data == nullptr || !data->inlineText().m_text_gesture_active)
  {
    return;
  }

  const std::size_t index = data->inlineText().m_text_target_index;
  const bool was_dragging = data->inlineText().m_text_dragging;
  const bool was_rotating = data->inlineText().m_text_rotating;
  const float rotation_degrees =
      data->inlineText().m_text_rotation_degrees;
  const float press_x = data->inlineText().m_text_press_x;
  const float press_y = data->inlineText().m_text_press_y;
  const float origin_x = data->inlineText().m_text_origin_x;
  const float origin_y = data->inlineText().m_text_origin_y;
  resetTextGesture(data);
  ReleaseCapture();

  if (index == kInvalidAnnotationIndex ||
      index >= data->core().m_session.engine().document().count())
  {
    return;
  }

  if (was_rotating)
  {
    Annotation updated =
        data->core().m_session.engine().document().items().at(index);
    if (updated.text_wrap_width <= 0.0f)
    {
      updated.text_wrap_width =
          (std::max)(1.0f, updated.bounds.width);
    }
    updated.rotation_degrees =
        annotationEditorNormalizeDegrees(rotation_degrees);
    if (data->core().m_session.engine().replaceAt(index, updated))
    {
      selectTextAnnotation(data, hwnd, index, x, y);
    }
    return;
  }

  if (was_dragging)
  {
    Annotation updated = data->core().m_session.engine().document().items().at(index);
    updated.start.x = origin_x + (static_cast<float>(x) - press_x);
    updated.start.y = origin_y + (static_cast<float>(y) - press_y);
    fillTextHitBounds(hwnd, updated);
    if (data->core().m_session.engine().replaceAt(index, updated))
    {
      selectTextAnnotation(data, hwnd, index, x, y);
    }
    return;
  }

  // 单击：选中；双击由下一次按下的 isTextDoubleClick 进入编辑。
  selectTextAnnotation(data, hwnd, index, x, y);
}
}  // namespace qingying
