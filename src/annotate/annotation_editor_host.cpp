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

void invalidateImageArea(const AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_overlay == nullptr)
  {
    return;
  }
  const Image& source = data->m_session.source();
  if (source.empty())
  {
    return;
  }
  RECT rect{data->m_image_origin_x, data->m_image_origin_y,
            data->m_image_origin_x + source.width,
            data->m_image_origin_y + source.height};
  (void)InflateRect(&rect, AnnotationEditorTextDeleteButtonPx,
                    AnnotationEditorTextDeleteButtonPx);
  InvalidateRect(data->m_overlay, &rect, FALSE);
}

void requestClose(AnnotationEditorHost* data, bool confirmed)
{
  if (data == nullptr || data->m_overlay == nullptr)
  {
    return;
  }
  data->m_confirmed = confirmed;
  PostMessageW(data->m_overlay, WM_CLOSE, 0, 0);
}

void finishAndNotify(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }

  AnnotationFinishResult result;
  if (data->m_confirmed)
  {
    if (!data->m_session.finishConfirmed(result))
    {
      result.cancelled = true;
      result.rendered_image = Image{};
    }
  }
  else
  {
    (void)data->m_session.finishCancelled(result);
  }

  if ((data->m_owner_suppress_callback == nullptr ||
       !*data->m_owner_suppress_callback) &&
      data->m_callback)
  {
    data->m_callback(result);
  }
}

bool pointInImageArea(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr)
  {
    return false;
  }
  const Image& source = data->m_session.source();
  const int local_x = x - data->m_image_origin_x;
  const int local_y = y - data->m_image_origin_y;
  return local_x >= 0 && local_y >= 0 && local_x < source.width &&
         local_y < source.height;
}

void canvasFromClient(const AnnotationEditorHost* data, int x, int y, float& out_x,
                      float& out_y)
{
  out_x = 0.0f;
  out_y = 0.0f;
  if (data == nullptr)
  {
    return;
  }
  out_x = static_cast<float>(x - data->m_image_origin_x);
  out_y = static_cast<float>(y - data->m_image_origin_y);
}

bool handleEditorKeyDown(HWND hwnd, AnnotationEditorHost* data, WPARAM key)
{
  if (data == nullptr)
  {
    return false;
  }
  if (key == VK_ESCAPE)
  {
    if (data->m_stroke_popup != nullptr &&
        IsWindowVisible(data->m_stroke_popup) != FALSE)
    {
      hideStrokePopup(data);
      return true;
    }
    if (data->m_inline_edit != nullptr)
    {
      cancelInlineText(data);
      return true;
    }
    if (data->m_text_gesture_active)
    {
      resetTextGesture(data);
      ReleaseCapture();
      invalidateImageArea(data);
      return true;
    }
    if (data->m_selected_text_index != kInvalidAnnotationIndex)
    {
      clearTextSelection(data);
      invalidateImageArea(data);
      return true;
    }
    requestClose(data, false);
    return true;
  }
  if ((key == VK_DELETE || key == VK_BACK) && data->m_inline_edit == nullptr)
  {
    if (deleteSelectedTextAnnotation(data))
    {
      return true;
    }
  }
  if (isCtrlZKey(key))
  {
    if (data->m_inline_edit != nullptr)
    {
      cancelInlineText(data);
    }
    clearTextSelection(data);
    if (data->m_controller.undo(data->m_session.engine()))
    {
      invalidateImageArea(data);
    }
    return true;
  }
  (void)hwnd;
  return false;
}

LRESULT CALLBACK editorWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                               LPARAM lparam)
{
  AnnotationEditorHost* data = reinterpret_cast<AnnotationEditorHost*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg)
  {
    case WM_NCCREATE:
    {
      const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      AnnotationEditorHost* create_data =
          static_cast<AnnotationEditorHost*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(create_data));
      if (create_data != nullptr)
      {
        create_data->m_overlay = hwnd;
      }
      return TRUE;
    }
    case WM_CREATE:
    {
      if (data == nullptr || !createButtons(hwnd, data))
      {
        return -1;
      }
      const HWND owner = GetWindow(hwnd, GW_OWNER);
      if (owner != nullptr)
      {
        (void)SetPropW(owner, kEditorHwndPropName, hwnd);
      }
      return 0;
    }
    case WM_PAINT:
    {
      const PaintGuard paint(hwnd);
      paintEditorBuffered(data, paint.dc());
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SETCURSOR:
    {
      if (data != nullptr && data->m_toolbar_hover >= 0 &&
          data->m_toolbar_items[static_cast<std::size_t>(data->m_toolbar_hover)]
                  .id == kButtonMoveId)
      {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32646)));  // IDC_SIZEALL
        return TRUE;
      }
      break;
    }
    case WM_KEYDOWN:
      if (handleEditorKeyDown(hwnd, data, wparam))
      {
        return 0;
      }
      break;
    case WM_CTLCOLOREDIT:
    {
      if (data == nullptr || data->m_inline_edit == nullptr)
      {
        break;
      }
      if (reinterpret_cast<HWND>(lparam) != data->m_inline_edit)
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
    case WM_LBUTTONDOWN:
    {
      if (data == nullptr)
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      const int hit = hitTestEditorToolbar(data, x, y);
      if (hit >= 0 &&
          data->m_toolbar_items[static_cast<std::size_t>(hit)].id ==
              kButtonMoveId)
      {
        beginChromeDrag(data, hwnd, x, y);
        return 0;
      }
      if (hit >= 0)
      {
        handleToolbarItemClick(data,
                               data->m_toolbar_items[static_cast<std::size_t>(hit)]
                                   .id);
        return 0;
      }
      if (hitTestChromeBar(data, x, y))
      {
        handlePropertyBarClick(data, x, y);
        return 0;
      }
      if (hitTestTextAnnotation(data, x, y) != kInvalidAnnotationIndex)
      {
        data->m_controller.setTool(AnnotationTool::Text);
        resizeEditorChrome(data);
        beginOrEditTextAt(data, hwnd, x, y);
        return 0;
      }
      if (!pointInImageArea(data, x, y))
      {
        return 0;
      }
      if (data->m_controller.tool() == AnnotationTool::Text)
      {
        beginOrEditTextAt(data, hwnd, x, y);
        return 0;
      }
      clearTextSelection(data);
      float canvas_x = 0.0f;
      float canvas_y = 0.0f;
      canvasFromClient(data, x, y, canvas_x, canvas_y);
      if (data->m_controller.beginStroke(canvas_x, canvas_y))
      {
        SetCapture(hwnd);
        invalidateImageArea(data);
      }
      return 0;
    }
    case WM_LBUTTONDBLCLK:
    {
      if (data == nullptr)
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      if (data->m_controller.tool() != AnnotationTool::Text &&
          hitTestTextAnnotation(data, x, y) == kInvalidAnnotationIndex)
      {
        return 0;
      }
      if (!pointInImageArea(data, x, y))
      {
        return 0;
      }
      commitInlineText(data);
      resetTextGesture(data);
      ReleaseCapture();
      const std::size_t hit = hitTestTextAnnotation(data, x, y);
      if (hit != kInvalidAnnotationIndex)
      {
        data->m_controller.setTool(AnnotationTool::Text);
        resizeEditorChrome(data);
        beginInlineText(data, hwnd, x, y, hit);
      }
      return 0;
    }
    case WM_MOUSEMOVE:
    {
      if (data == nullptr)
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      if (data->m_chrome_dragging)
      {
        updateChromeDrag(data, x, y);
        return 0;
      }
      if (hitTestChromeBar(data, x, y) &&
          !data->m_controller.isDrawing() && !data->m_text_gesture_active)
      {
        const int hit = hitTestEditorToolbar(data, x, y);
        const bool chip_hover = hitTestStrokeChip(data, x, y);
        if (hit != data->m_toolbar_hover ||
            chip_hover != data->m_stroke_chip_hover)
        {
          data->m_toolbar_hover = hit;
          data->m_stroke_chip_hover = chip_hover;
          invalidateToolbar(data);
        }
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        TrackMouseEvent(&track);
        return 0;
      }
      if (data->m_toolbar_hover >= 0 || data->m_stroke_chip_hover)
      {
        data->m_toolbar_hover = -1;
        data->m_stroke_chip_hover = false;
        invalidateToolbar(data);
      }
      if (data->m_text_gesture_active)
      {
        updateTextGesture(data, x, y);
        return 0;
      }
      if (!data->m_controller.isDrawing())
      {
        return 0;
      }
      float canvas_x = 0.0f;
      float canvas_y = 0.0f;
      canvasFromClient(data, x, y, canvas_x, canvas_y);
      data->m_controller.updateStroke(canvas_x, canvas_y);
      invalidateImageArea(data);
      return 0;
    }
    case WM_MOUSELEAVE:
      if (data != nullptr &&
          (data->m_toolbar_hover >= 0 || data->m_stroke_chip_hover))
      {
        data->m_toolbar_hover = -1;
        data->m_stroke_chip_hover = false;
        invalidateToolbar(data);
      }
      return 0;
    case WM_MOUSEWHEEL:
    {
      if (data == nullptr)
      {
        return 0;
      }
      POINT pt{};
      pt.x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      pt.y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      ScreenToClient(hwnd, &pt);
      const int delta =
          static_cast<int>(static_cast<short>(HIWORD(wparam)));
      if (hitTestStrokeChip(data, pt.x, pt.y) ||
          (data->m_stroke_popup != nullptr &&
           IsWindowVisible(data->m_stroke_popup) != FALSE))
      {
        (void)handleStrokeChipWheel(data, delta);
        return 0;
      }
      if (annotationEditorWheelAdjustsSize(
              data->m_controller.tool(), hitTestSizeCombo(data, pt.x, pt.y)))
      {
        (void)handleSizeComboWheel(data, delta);
        return 0;
      }
      break;
    }
    case WM_LBUTTONUP:
    {
      if (data == nullptr)
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      if (data->m_chrome_dragging)
      {
        endChromeDrag(data);
        return 0;
      }
      if (data->m_text_gesture_active)
      {
        finishTextGesture(data, hwnd, x, y);
        return 0;
      }
      if (!data->m_controller.isDrawing())
      {
        return 0;
      }
      float canvas_x = 0.0f;
      float canvas_y = 0.0f;
      canvasFromClient(data, x, y, canvas_x, canvas_y);
      data->m_controller.updateStroke(canvas_x, canvas_y);
      (void)data->m_controller.endStroke(data->m_session.engine());
      ReleaseCapture();
      invalidateImageArea(data);
      return 0;
    }
    case WM_COMMAND:
    {
      if (data == nullptr)
      {
        return 0;
      }

      const UINT id = LOWORD(wparam);
      const UINT code = HIWORD(wparam);
      const HWND combo = reinterpret_cast<HWND>(lparam);
      if ((id == kFontComboId || combo == data->m_font_combo) &&
          (code == CBN_SELCHANGE || code == CBN_SELENDOK))
      {
        syncSizeFromCombo(data);
        return 0;
      }
      if (id == kInlineEditId && code == EN_CHANGE)
      {
        layoutInlineEdit(data);
        return 0;
      }
      if (id == kInlineEditId && code == EN_KILLFOCUS)
      {
        const HWND focus = GetFocus();
        if (focus == data->m_font_combo || pointerHitsStyleChrome(data))
        {
          return 0;
        }
        commitInlineText(data);
        return 0;
      }
      if (code != BN_CLICKED)
      {
        return 0;
      }
      if (id == kButtonConfirmId)
      {
        commitInlineText(data);
        requestClose(data, true);
      }
      else if (id == kButtonCancelId)
      {
        cancelInlineText(data);
        requestClose(data, false);
      }
      else
      {
        handleToolCommand(data, id);
      }
      return 0;
    }
    case kMsgCancelFromBackdrop:
      if (data != nullptr)
      {
        cancelInlineText(data);
        resetTextGesture(data);
        clearTextSelection(data);
        data->m_confirmed = false;
        DestroyWindow(hwnd);
      }
      return 0;
    case WM_CLOSE:
      commitInlineText(data);
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      if (data != nullptr)
      {
        const HWND owner = GetWindow(hwnd, GW_OWNER);
        if (owner != nullptr)
        {
          RemovePropW(owner, kEditorHwndPropName);
        }
        if (data->m_tooltip != nullptr)
        {
          DestroyWindow(data->m_tooltip);
          data->m_tooltip = nullptr;
        }
        destroyInlineEdit(data);
        destroyStrokePopup(data);
        data->m_combo_font.reset();
        finishAndNotify(data);
        if (data->m_owner_hwnd != nullptr)
        {
          *data->m_owner_hwnd = nullptr;
        }
        if (data->m_owner_visible != nullptr)
        {
          *data->m_owner_visible = false;
        }
        if (data->m_owner_suppress_callback != nullptr)
        {
          *data->m_owner_suppress_callback = false;
        }
      }
      return 0;
    default:
      break;
  }

  if (msg == WM_NCDESTROY)
  {
    if (data != nullptr && data->m_destroyed_during_create != nullptr)
    {
      *data->m_destroyed_during_create = true;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    if (data != nullptr && data->m_destroyed_during_create == nullptr)
    {
      std::unique_ptr<AnnotationEditorHost> owned(data);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }

  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool registerEditorClass(HINSTANCE instance)
{
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.style = CS_DBLCLKS;
  wc.lpfnWndProc = editorWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
  wc.lpszClassName = kOverlayClassName;

  return RegisterClassExW(&wc) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
}  // namespace qingying
