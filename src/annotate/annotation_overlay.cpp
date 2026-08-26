#include "qingying/annotate/annotation_overlay.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

#include "qingying/annotate/annotation_editor_layout.hpp"
#include "qingying/annotate/annotation_editor_session.hpp"
#include "qingying/annotate/annotation_interaction_controller.hpp"
#include "qingying/annotate/annotation_renderer.hpp"
#include "qingying/annotate/annotation_types.hpp"
#include "qingying/ui/modern_toolbar.hpp"

namespace qingying {

namespace {

const wchar_t kOverlayClassName[] = L"QingYingAnnotationOverlay";

constexpr UINT kButtonConfirmId = 1;
constexpr UINT kButtonCancelId = 2;
constexpr UINT kButtonRectId = 3;
constexpr UINT kButtonEllipseId = 4;
constexpr UINT kButtonArrowId = 5;
constexpr UINT kButtonPenId = 6;
constexpr UINT kButtonTextId = 7;
constexpr UINT kButtonUndoId = 8;
constexpr UINT kButtonMosaicId = 9;
constexpr UINT kFontComboId = 9;
constexpr UINT kInlineEditId = 10;

constexpr int kInlineTextMaxChars = 256;
constexpr int kInlineEditMinWidth = 80;
constexpr int kInlineEditMaxWidth = 220;
constexpr int kInlineEditHeightPad = 10;
constexpr int kTextDragThresholdPx = 4;
constexpr int kTextHitPaddingPx = 10;
constexpr int kTextMinHitWidthPx = 28;
constexpr int kTextMinHitHeightPx = 20;
constexpr int kTextSelectionPadPx = 3;
constexpr std::size_t kInvalidAnnotationIndex =
    static_cast<std::size_t>(-1);

constexpr int kToolbarIconItemCount = 9;

struct EditorToolbarItem
{
  UINT id{0};
  ToolbarIconKind icon{ToolbarIconKind::Rectangle};
  bool accent{false};
  RECT rect{};
};

struct EditorWindowData
{
  AnnotationEditorSession session;
  AnnotationInteractionController controller;
  AnnotationRenderer renderer;
  AnnotationCallback callback;
  HWND overlay{nullptr};
  HWND font_combo{nullptr};
  HWND inline_edit{nullptr};
  WNDPROC inline_edit_prev_proc{nullptr};
  HFONT inline_edit_font{nullptr};
  float text_anchor_x{0.0f};
  float text_anchor_y{0.0f};
  int font_size{DefaultFontSize};
  std::size_t editing_text_index{kInvalidAnnotationIndex};
  std::size_t selected_text_index{kInvalidAnnotationIndex};
  DWORD last_text_click_tick{0};
  std::size_t last_text_click_index{kInvalidAnnotationIndex};
  int last_text_click_x{0};
  int last_text_click_y{0};
  bool inline_edit_pressing{false};
  int inline_edit_press_x{0};
  int inline_edit_press_y{0};
  bool text_gesture_active{false};
  bool text_dragging{false};
  std::size_t text_target_index{kInvalidAnnotationIndex};
  float text_press_x{0.0f};
  float text_press_y{0.0f};
  float text_origin_x{0.0f};
  float text_origin_y{0.0f};
  float text_drag_x{0.0f};
  float text_drag_y{0.0f};
  EditorToolbarItem toolbar_items[kToolbarIconItemCount]{};
  int toolbar_hover{-1};
  int toolbar_divider_x[AnnotationEditorDividerCount]{};
  HWND tooltip{nullptr};
  wchar_t tooltip_text[kToolbarIconItemCount + 1][kToolbarTooltipMaxChars]{};
  bool confirmed{false};
  int client_width{0};
  bool* loop_done{nullptr};
};

class PaintGuard
{
 public:
  explicit PaintGuard(HWND hwnd) : m_hwnd(hwnd)
  {
    m_dc = BeginPaint(hwnd, &m_paint);
  }

  ~PaintGuard()
  {
    if (m_dc != nullptr)
    {
      EndPaint(m_hwnd, &m_paint);
    }
  }

  PaintGuard(const PaintGuard&) = delete;
  PaintGuard& operator=(const PaintGuard&) = delete;

  HDC dc() const
  {
    return m_dc;
  }

 private:
  HWND m_hwnd{nullptr};
  PAINTSTRUCT m_paint{};
  HDC m_dc{nullptr};
};

void blitImage(HDC hdc, const Image& image)
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

  (void)SetDIBitsToDevice(hdc, 0, 0, static_cast<DWORD>(image.width),
                          static_cast<DWORD>(image.height), 0, 0, 0,
                          static_cast<UINT>(image.height),
                          image.pixels.data(), &bmi, DIB_RGB_COLORS);
}

void invalidateImageArea(HWND hwnd, const Image& source)
{
  if (hwnd == nullptr || source.empty())
  {
    return;
  }
  RECT rect{0, 0, source.width, source.height};
  InvalidateRect(hwnd, &rect, FALSE);
}

void destroyInlineEdit(EditorWindowData* data);
bool measureAnnotationText(HDC hdc, const Annotation& annotation, SIZE& out_size);
void fillTextHitBounds(HWND hwnd, Annotation& annotation);
void commitInlineText(EditorWindowData* data);
void cancelInlineText(EditorWindowData* data);
void resetTextGesture(EditorWindowData* data);
void clearTextSelection(EditorWindowData* data);
void selectTextAnnotation(EditorWindowData* data, HWND hwnd, std::size_t index,
                          int click_x, int click_y);
bool isTextDoubleClick(const EditorWindowData* data, std::size_t hit, int x,
                       int y);
bool deleteSelectedTextAnnotation(EditorWindowData* data);
void drawTextSelectionFrame(HDC hdc, EditorWindowData* data);
void paintEditorToolbar(HDC hdc, EditorWindowData* data);
void invalidateToolbar(EditorWindowData* data);
int hitTestEditorToolbar(const EditorWindowData* data, int x, int y);
void handleToolCommand(EditorWindowData* data, UINT id);
void handleToolbarItemClick(EditorWindowData* data, UINT id);
bool createButtons(HWND hwnd, EditorWindowData* data);
void updateTextGesture(EditorWindowData* data, int x, int y);
void tryPromoteInlineEditToDrag(EditorWindowData* data, int client_x,
                                int client_y);

void destroyInlineEdit(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }

  data->inline_edit_pressing = false;

  if (data->inline_edit != nullptr)
  {
    // 销毁前释放捕获，避免后续手势/父窗口收不到鼠标消息。
    if (GetCapture() == data->inline_edit)
    {
      ReleaseCapture();
    }
    if (data->inline_edit_prev_proc != nullptr)
    {
      SetWindowLongPtrW(data->inline_edit, GWLP_WNDPROC,
                        reinterpret_cast<LONG_PTR>(data->inline_edit_prev_proc));
      data->inline_edit_prev_proc = nullptr;
    }
    DestroyWindow(data->inline_edit);
    data->inline_edit = nullptr;
  }

  if (data->inline_edit_font != nullptr)
  {
    DeleteObject(data->inline_edit_font);
    data->inline_edit_font = nullptr;
  }
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

void commitInlineText(EditorWindowData* data)
{
  if (data == nullptr || data->inline_edit == nullptr)
  {
    return;
  }

  wchar_t buffer[kInlineTextMaxChars]{};
  GetWindowTextW(data->inline_edit, buffer, kInlineTextMaxChars);
  const float anchor_x = data->text_anchor_x;
  const float anchor_y = data->text_anchor_y;
  const int font_size = data->font_size;
  const std::size_t edit_index = data->editing_text_index;
  HWND overlay = data->overlay;
  destroyInlineEdit(data);
  data->editing_text_index = kInvalidAnnotationIndex;

  if (buffer[0] == L'\0')
  {
    return;
  }

  Annotation annotation;
  annotation.type = AnnotationType::Text;
  annotation.start = PointF{anchor_x, anchor_y};
  annotation.text.assign(buffer);
  annotation.style = AnnotationStyle{};
  annotation.style.font_size = font_size;
  fillTextHitBounds(overlay, annotation);

  bool ok = false;
  if (edit_index != kInvalidAnnotationIndex)
  {
    ok = data->session.engine().replaceAt(edit_index, annotation);
  }
  else
  {
    ok = data->session.engine().add(annotation);
  }

  if (ok)
  {
    invalidateImageArea(overlay, data->session.source());
  }
}

void cancelInlineText(EditorWindowData* data)
{
  destroyInlineEdit(data);
  if (data != nullptr)
  {
    data->editing_text_index = kInvalidAnnotationIndex;
  }
}

void resetTextGesture(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->text_gesture_active = false;
  data->text_dragging = false;
  data->text_target_index = kInvalidAnnotationIndex;
}

void clearTextSelection(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->selected_text_index = kInvalidAnnotationIndex;
  data->last_text_click_index = kInvalidAnnotationIndex;
  data->last_text_click_tick = 0;
}

void selectTextAnnotation(EditorWindowData* data, HWND hwnd, std::size_t index,
                          int click_x, int click_y)
{
  if (data == nullptr || hwnd == nullptr)
  {
    return;
  }
  data->selected_text_index = index;
  data->last_text_click_index = index;
  data->last_text_click_tick = GetTickCount();
  data->last_text_click_x = click_x;
  data->last_text_click_y = click_y;
  SetFocus(hwnd);
  invalidateImageArea(hwnd, data->session.source());
}

bool isTextDoubleClick(const EditorWindowData* data, std::size_t hit, int x,
                       int y)
{
  if (data == nullptr || hit == kInvalidAnnotationIndex ||
      data->last_text_click_index != hit)
  {
    return false;
  }

  const DWORD elapsed = GetTickCount() - data->last_text_click_tick;
  if (elapsed > GetDoubleClickTime())
  {
    return false;
  }

  const int limit_x = GetSystemMetrics(SM_CXDOUBLECLK) / 2;
  const int limit_y = GetSystemMetrics(SM_CYDOUBLECLK) / 2;
  const int dx = x - data->last_text_click_x;
  const int dy = y - data->last_text_click_y;
  return dx >= -limit_x && dx <= limit_x && dy >= -limit_y && dy <= limit_y;
}

bool deleteSelectedTextAnnotation(EditorWindowData* data)
{
  if (data == nullptr || data->inline_edit != nullptr)
  {
    return false;
  }
  if (data->selected_text_index == kInvalidAnnotationIndex ||
      data->selected_text_index >= data->session.engine().document().count())
  {
    return false;
  }

  const std::size_t index = data->selected_text_index;
  if (!data->session.engine().removeAt(index))
  {
    return false;
  }

  clearTextSelection(data);
  resetTextGesture(data);
  invalidateImageArea(data->overlay, data->session.source());
  return true;
}

void selectFontSizeInCombo(EditorWindowData* data, int font_size)
{
  if (data == nullptr)
  {
    return;
  }

  data->font_size = font_size;
  if (data->font_combo == nullptr)
  {
    return;
  }

  for (int i = 0; i < AnnotationEditorFontSizeOptionCount; ++i)
  {
    if (AnnotationEditorFontSizeOptions[i] == font_size)
    {
      SendMessageW(data->font_combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
      return;
    }
  }
}

bool measureAnnotationText(HDC hdc, const Annotation& annotation, SIZE& out_size)
{
  out_size.cx = 0;
  out_size.cy = 0;
  if (hdc == nullptr || annotation.text.empty())
  {
    return false;
  }

  const int font_px = (std::min)((std::max)(annotation.style.font_size, MinFontSize),
                                 MaxFontSize);
  HFONT font = CreateFontW(
      -font_px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  if (font == nullptr)
  {
    return false;
  }

  const HGDIOBJ old = SelectObject(hdc, font);
  const BOOL ok = GetTextExtentPoint32W(
      hdc, annotation.text.c_str(), static_cast<int>(annotation.text.size()),
      &out_size);
  SelectObject(hdc, old);
  DeleteObject(font);
  return ok != FALSE;
}

std::size_t hitTestTextAnnotation(EditorWindowData* data, int x, int y)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return kInvalidAnnotationIndex;
  }

  const HDC hdc = GetDC(data->overlay);
  if (hdc == nullptr)
  {
    return kInvalidAnnotationIndex;
  }

  const auto& items = data->session.engine().document().items();
  std::size_t hit = kInvalidAnnotationIndex;
  for (std::size_t i = items.size(); i > 0; --i)
  {
    const std::size_t index = i - 1;
    const Annotation& annotation = items.at(index);
    if (annotation.type != AnnotationType::Text)
    {
      continue;
    }

    int width = static_cast<int>(annotation.bounds.width);
    int height = static_cast<int>(annotation.bounds.height);
    if (width < kTextMinHitWidthPx || height < kTextMinHitHeightPx)
    {
      SIZE size{};
      if (measureAnnotationText(hdc, annotation, size))
      {
        width = (std::max)(static_cast<int>(size.cx), kTextMinHitWidthPx);
        height = (std::max)(static_cast<int>(size.cy), kTextMinHitHeightPx);
      }
      else
      {
        width = kTextMinHitWidthPx;
        height = kTextMinHitHeightPx;
      }
    }

    const int left =
        static_cast<int>(annotation.start.x) - kTextHitPaddingPx;
    const int top = static_cast<int>(annotation.start.y) - kTextHitPaddingPx;
    const int right = static_cast<int>(annotation.start.x) + width +
                      kTextHitPaddingPx;
    const int bottom = static_cast<int>(annotation.start.y) + height +
                       kTextHitPaddingPx;
    if (x >= left && x < right && y >= top && y < bottom)
    {
      hit = index;
      break;
    }
  }

  ReleaseDC(data->overlay, hdc);
  return hit;
}

void tryPromoteInlineEditToDrag(EditorWindowData* data, int client_x,
                                int client_y)
{
  if (data == nullptr || data->inline_edit == nullptr ||
      data->editing_text_index == kInvalidAnnotationIndex ||
      data->editing_text_index >= data->session.engine().document().count())
  {
    return;
  }

  const int dx = client_x - data->inline_edit_press_x;
  const int dy = client_y - data->inline_edit_press_y;
  if (dx * dx + dy * dy < kTextDragThresholdPx * kTextDragThresholdPx)
  {
    return;
  }

  // 先把编辑框里的文案写回文档，再进入拖拽，避免未提交内容丢失。
  wchar_t buffer[kInlineTextMaxChars]{};
  GetWindowTextW(data->inline_edit, buffer, kInlineTextMaxChars);
  const std::size_t index = data->editing_text_index;
  Annotation updated =
      data->session.engine().document().items().at(index);
  if (buffer[0] != L'\0')
  {
    updated.text.assign(buffer);
    updated.style.font_size = data->font_size;
    fillTextHitBounds(data->overlay, updated);
    (void)data->session.engine().replaceAt(index, updated);
  }

  POINT press{data->inline_edit_press_x, data->inline_edit_press_y};
  ClientToScreen(data->inline_edit, &press);
  ScreenToClient(data->overlay, &press);

  POINT current{client_x, client_y};
  ClientToScreen(data->inline_edit, &current);
  ScreenToClient(data->overlay, &current);

  destroyInlineEdit(data);
  data->editing_text_index = kInvalidAnnotationIndex;

  data->text_gesture_active = true;
  data->text_dragging = true;
  data->text_target_index = index;
  data->text_press_x = static_cast<float>(press.x);
  data->text_press_y = static_cast<float>(press.y);
  data->text_origin_x = updated.start.x;
  data->text_origin_y = updated.start.y;
  data->text_drag_x =
      updated.start.x + static_cast<float>(current.x - press.x);
  data->text_drag_y =
      updated.start.y + static_cast<float>(current.y - press.y);
  SetCapture(data->overlay);
  invalidateImageArea(data->overlay, data->session.source());
}

LRESULT CALLBACK inlineEditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                        LPARAM lparam)
{
  EditorWindowData* data = reinterpret_cast<EditorWindowData*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  const WNDPROC prev =
      data != nullptr ? data->inline_edit_prev_proc : nullptr;

  if (data != nullptr)
  {
    if (msg == WM_LBUTTONDOWN)
    {
      data->inline_edit_pressing = true;
      data->inline_edit_press_x =
          static_cast<int>(static_cast<short>(LOWORD(lparam)));
      data->inline_edit_press_y =
          static_cast<int>(static_cast<short>(HIWORD(lparam)));
      // 必须捕获：拖出 EDIT 客户区后否则收不到 MOVE，无法晋升为标注拖拽。
      SetCapture(hwnd);
    }
    else if (msg == WM_MOUSEMOVE && data->inline_edit_pressing &&
             (wparam & MK_LBUTTON) != 0)
    {
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      tryPromoteInlineEditToDrag(data, x, y);
      if (data->text_dragging)
      {
        return 0;
      }
    }
    else if (msg == WM_LBUTTONUP)
    {
      data->inline_edit_pressing = false;
      if (GetCapture() == hwnd)
      {
        ReleaseCapture();
      }
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
        cancelInlineText(data);
        return 0;
      }
    }
  }

  if (prev != nullptr)
  {
    return CallWindowProcW(prev, hwnd, msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void beginInlineText(EditorWindowData* data, HWND hwnd, int x, int y,
                     std::size_t edit_index)
{
  if (data == nullptr)
  {
    return;
  }

  commitInlineText(data);
  resetTextGesture(data);
  clearTextSelection(data);

  const Image& source = data->session.source();
  std::wstring initial_text;
  data->editing_text_index = edit_index;
  data->text_anchor_x = static_cast<float>(x);
  data->text_anchor_y = static_cast<float>(y);

  if (edit_index != kInvalidAnnotationIndex &&
      edit_index < data->session.engine().document().count())
  {
    const Annotation& existing =
        data->session.engine().document().items().at(edit_index);
    data->text_anchor_x = existing.start.x;
    data->text_anchor_y = existing.start.y;
    initial_text = existing.text;
    selectFontSizeInCombo(data, existing.style.font_size);
  }

  const int edit_x = static_cast<int>(data->text_anchor_x);
  const int edit_y = static_cast<int>(data->text_anchor_y);
  const int edit_height = data->font_size + kInlineEditHeightPad;
  const int remain_width = source.width - edit_x;
  int edit_width = (std::min)(kInlineEditMaxWidth, remain_width);
  edit_width = (std::max)(kInlineEditMinWidth, edit_width);
  if (edit_x + edit_width > source.width)
  {
    edit_width = (std::max)(1, source.width - edit_x);
  }

  data->inline_edit = CreateWindowExW(
      WS_EX_CLIENTEDGE, L"EDIT", initial_text.c_str(),
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, edit_x, edit_y,
      edit_width, edit_height, hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInlineEditId)),
      GetModuleHandleW(nullptr), nullptr);
  if (data->inline_edit == nullptr)
  {
    data->editing_text_index = kInvalidAnnotationIndex;
    return;
  }

  SendMessageW(data->inline_edit, EM_SETLIMITTEXT, kInlineTextMaxChars - 1, 0);
  data->inline_edit_font = CreateFontW(
      -data->font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  if (data->inline_edit_font != nullptr)
  {
    SendMessageW(data->inline_edit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(data->inline_edit_font), TRUE);
  }

  SetWindowLongPtrW(data->inline_edit, GWLP_USERDATA,
                    reinterpret_cast<LONG_PTR>(data));
  data->inline_edit_prev_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
      data->inline_edit, GWLP_WNDPROC,
      reinterpret_cast<LONG_PTR>(inlineEditSubclassProc)));
  const int text_len = static_cast<int>(initial_text.size());
  SendMessageW(data->inline_edit, EM_SETSEL, text_len, text_len);
  SetFocus(data->inline_edit);
  invalidateImageArea(hwnd, source);
}

void beginOrEditTextAt(EditorWindowData* data, HWND hwnd, int x, int y)
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
    if (isTextDoubleClick(data, hit, x, y))
    {
      data->last_text_click_index = kInvalidAnnotationIndex;
      beginInlineText(data, hwnd, x, y, hit);
      return;
    }

    const Annotation& existing =
        data->session.engine().document().items().at(hit);
    data->text_gesture_active = true;
    data->text_dragging = false;
    data->text_target_index = hit;
    data->text_press_x = static_cast<float>(x);
    data->text_press_y = static_cast<float>(y);
    data->text_origin_x = existing.start.x;
    data->text_origin_y = existing.start.y;
    data->text_drag_x = existing.start.x;
    data->text_drag_y = existing.start.y;
    SetCapture(hwnd);
    return;
  }

  clearTextSelection(data);
  beginInlineText(data, hwnd, x, y, kInvalidAnnotationIndex);
}

void updateTextGesture(EditorWindowData* data, int x, int y)
{
  if (data == nullptr || !data->text_gesture_active)
  {
    return;
  }

  const float dx = static_cast<float>(x) - data->text_press_x;
  const float dy = static_cast<float>(y) - data->text_press_y;
  if (!data->text_dragging)
  {
    const float distance_sq = dx * dx + dy * dy;
    const float threshold = static_cast<float>(kTextDragThresholdPx *
                                               kTextDragThresholdPx);
    if (distance_sq < threshold)
    {
      return;
    }
    data->text_dragging = true;
  }

  data->text_drag_x = data->text_origin_x + dx;
  data->text_drag_y = data->text_origin_y + dy;
  invalidateImageArea(data->overlay, data->session.source());
}

void finishTextGesture(EditorWindowData* data, HWND hwnd, int x, int y)
{
  if (data == nullptr || !data->text_gesture_active)
  {
    return;
  }

  const std::size_t index = data->text_target_index;
  const bool was_dragging = data->text_dragging;
  const float press_x = data->text_press_x;
  const float press_y = data->text_press_y;
  const float origin_x = data->text_origin_x;
  const float origin_y = data->text_origin_y;
  resetTextGesture(data);
  ReleaseCapture();

  if (index == kInvalidAnnotationIndex ||
      index >= data->session.engine().document().count())
  {
    return;
  }

  if (was_dragging)
  {
    Annotation updated = data->session.engine().document().items().at(index);
    updated.start.x = origin_x + (static_cast<float>(x) - press_x);
    updated.start.y = origin_y + (static_cast<float>(y) - press_y);
    fillTextHitBounds(hwnd, updated);
    if (data->session.engine().replaceAt(index, updated))
    {
      selectTextAnnotation(data, hwnd, index, x, y);
    }
    return;
  }

  // 单击：选中；双击由下一次按下的 isTextDoubleClick 进入编辑。
  selectTextAnnotation(data, hwnd, index, x, y);
}

void syncFontSizeFromCombo(EditorWindowData* data)
{
  if (data == nullptr || data->font_combo == nullptr)
  {
    return;
  }

  const LRESULT index = SendMessageW(data->font_combo, CB_GETCURSEL, 0, 0);
  if (index == CB_ERR || index < 0 ||
      index >= AnnotationEditorFontSizeOptionCount)
  {
    return;
  }

  data->font_size =
      AnnotationEditorFontSizeOptions[static_cast<std::size_t>(index)];
  if (data->inline_edit != nullptr)
  {
    if (data->inline_edit_font != nullptr)
    {
      DeleteObject(data->inline_edit_font);
      data->inline_edit_font = nullptr;
    }
    data->inline_edit_font = CreateFontW(
        -data->font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    if (data->inline_edit_font != nullptr)
    {
      SendMessageW(data->inline_edit, WM_SETFONT,
                   reinterpret_cast<WPARAM>(data->inline_edit_font), TRUE);
    }
  }
}

void paintEditor(EditorWindowData* data, HDC hdc)
{
  if (data == nullptr || hdc == nullptr)
  {
    return;
  }

  Image composed;
  const Annotation* preview =
      data->controller.hasPreview() ? &data->controller.preview() : nullptr;

  const bool relocate =
      data->text_dragging &&
      data->text_target_index != kInvalidAnnotationIndex &&
      data->text_target_index < data->session.engine().document().count();
  const bool hide_editing =
      data->inline_edit != nullptr &&
      data->editing_text_index != kInvalidAnnotationIndex &&
      data->editing_text_index < data->session.engine().document().count();

  if (relocate || hide_editing)
  {
    AnnotationDocument temp;
    const auto& items = data->session.engine().document().items();
    for (std::size_t i = 0; i < items.size(); ++i)
    {
      if (hide_editing && i == data->editing_text_index)
      {
        continue;
      }
      Annotation item = items.at(i);
      if (relocate && i == data->text_target_index)
      {
        item.start.x = data->text_drag_x;
        item.start.y = data->text_drag_y;
        item.bounds.x = item.start.x;
        item.bounds.y = item.start.y;
      }
      (void)temp.add(item);
    }
    if (!data->renderer.rasterize(data->session.source(), temp, preview,
                                  composed))
    {
      return;
    }
  }
  else if (!data->renderer.rasterize(data->session.source(),
                                     data->session.engine().document(), preview,
                                     composed))
  {
    return;
  }

  blitImage(hdc, composed);
  drawTextSelectionFrame(hdc, data);
  paintEditorToolbar(hdc, data);
}

void drawTextSelectionFrame(HDC hdc, EditorWindowData* data)
{
  if (hdc == nullptr || data == nullptr ||
      data->selected_text_index == kInvalidAnnotationIndex ||
      data->selected_text_index >= data->session.engine().document().count() ||
      data->inline_edit != nullptr)
  {
    return;
  }

  Annotation annotation =
      data->session.engine().document().items().at(data->selected_text_index);
  if (annotation.type != AnnotationType::Text)
  {
    return;
  }

  if (data->text_dragging &&
      data->text_target_index == data->selected_text_index)
  {
    annotation.start.x = data->text_drag_x;
    annotation.start.y = data->text_drag_y;
  }

  SIZE size{};
  if (!measureAnnotationText(hdc, annotation, size))
  {
    size.cx = kTextMinHitWidthPx;
    size.cy = kTextMinHitHeightPx;
  }
  size.cx = (std::max)(static_cast<int>(size.cx), kTextMinHitWidthPx);
  size.cy = (std::max)(static_cast<int>(size.cy), kTextMinHitHeightPx);

  const int left = static_cast<int>(annotation.start.x) - kTextSelectionPadPx;
  const int top = static_cast<int>(annotation.start.y) - kTextSelectionPadPx;
  const int right = static_cast<int>(annotation.start.x) + size.cx +
                    kTextSelectionPadPx;
  const int bottom = static_cast<int>(annotation.start.y) + size.cy +
                     kTextSelectionPadPx;

  const HPEN pen = CreatePen(PS_DOT, 1, RGB(30, 144, 255));
  if (pen == nullptr)
  {
    return;
  }
  const HGDIOBJ old_pen = SelectObject(hdc, pen);
  const HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, left, top, right, bottom);
  SelectObject(hdc, old_brush);
  SelectObject(hdc, old_pen);
  DeleteObject(pen);
}

void paintEditorToolbar(HDC hdc, EditorWindowData* data)
{
  if (hdc == nullptr || data == nullptr)
  {
    return;
  }

  const Image& source = data->session.source();
  const int bar_top = source.height;
  const int bar_height = annotationEditorToolbarHeight();
  RECT strip{0, bar_top, data->client_width, bar_top + bar_height};
  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const HBRUSH brush = CreateSolidBrush(colors.bar_fill);
  if (brush != nullptr)
  {
    FillRect(hdc, &strip, brush);
    DeleteObject(brush);
  }

  RECT bar{AnnotationEditorBarPadding / 2, bar_top + 2,
           data->client_width - AnnotationEditorBarPadding / 2,
           bar_top + bar_height - 2};
  drawToolbarBar(hdc, bar);

  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    const EditorToolbarItem& item = data->toolbar_items[i];
    bool selected = false;
    switch (item.id)
    {
      case kButtonRectId:
        selected = data->controller.tool() == AnnotationTool::Rectangle;
        break;
      case kButtonEllipseId:
        selected = data->controller.tool() == AnnotationTool::Ellipse;
        break;
      case kButtonArrowId:
        selected = data->controller.tool() == AnnotationTool::Arrow;
        break;
      case kButtonPenId:
        selected = data->controller.tool() == AnnotationTool::Pen;
        break;
      case kButtonMosaicId:
        selected = data->controller.tool() == AnnotationTool::Mosaic;
        break;
      case kButtonTextId:
        selected = data->controller.tool() == AnnotationTool::Text;
        break;
      default:
        break;
    }
    drawToolbarItem(hdc, item.rect, item.icon, i == data->toolbar_hover,
                    selected, true, item.accent);
  }

  const int divider_top = bar.top + 8;
  const int divider_bottom = bar.bottom - 8;
  for (int i = 0; i < AnnotationEditorDividerCount; ++i)
  {
    if (data->toolbar_divider_x[i] > 0)
    {
      drawToolbarDivider(hdc, data->toolbar_divider_x[i], divider_top,
                         divider_bottom);
    }
  }
}

bool createButtons(HWND hwnd, EditorWindowData* data)
{
  if (data == nullptr)
  {
    return false;
  }

  const Image& source = data->session.source();
  const int client_width = data->client_width;
  const int y = source.height + AnnotationEditorBarPadding;
  int x = AnnotationEditorBarPadding;

  const struct
  {
    UINT id;
    ToolbarIconKind icon;
    bool accent;
  } left_items[] = {
      {kButtonRectId, ToolbarIconKind::Rectangle, false},
      {kButtonEllipseId, ToolbarIconKind::Ellipse, false},
      {kButtonArrowId, ToolbarIconKind::Arrow, false},
      {kButtonPenId, ToolbarIconKind::Pen, false},
      {kButtonMosaicId, ToolbarIconKind::Mosaic, false},
      {kButtonTextId, ToolbarIconKind::Text, false},
  };

  int item_index = 0;
  for (const auto& spec : left_items)
  {
    data->toolbar_items[item_index].id = spec.id;
    data->toolbar_items[item_index].icon = spec.icon;
    data->toolbar_items[item_index].accent = spec.accent;
    data->toolbar_items[item_index].rect = {
        x, y, x + AnnotationEditorButtonWidth, y + AnnotationEditorButtonHeight};
    x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
    ++item_index;
  }

  data->toolbar_divider_x[0] =
      x - AnnotationEditorButtonGap + AnnotationEditorDividerGap / 2;
  x += AnnotationEditorDividerGap - AnnotationEditorButtonGap;

  data->font_combo = CreateWindowExW(
      0, L"COMBOBOX", L"",
      WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, x, y,
      AnnotationEditorFontComboWidth, AnnotationEditorFontComboDropHeight, hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontComboId)),
      GetModuleHandleW(nullptr), nullptr);
  if (data->font_combo == nullptr)
  {
    return false;
  }

  int default_index = 1;  // 16
  for (int i = 0; i < AnnotationEditorFontSizeOptionCount; ++i)
  {
    wchar_t label[16]{};
    swprintf_s(label, L"%d", AnnotationEditorFontSizeOptions[i]);
    SendMessageW(data->font_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(label));
    if (AnnotationEditorFontSizeOptions[i] == DefaultFontSize)
    {
      default_index = i;
    }
  }
  SendMessageW(data->font_combo, CB_SETCURSEL,
               static_cast<WPARAM>(default_index), 0);
  data->font_size = DefaultFontSize;
  x += AnnotationEditorFontComboWidth + AnnotationEditorButtonGap;

  data->toolbar_items[item_index] = {
      kButtonUndoId,
      ToolbarIconKind::Undo,
      false,
      {x, y, x + AnnotationEditorButtonWidth, y + AnnotationEditorButtonHeight}};
  ++item_index;
  x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  data->toolbar_divider_x[1] =
      x - AnnotationEditorButtonGap + AnnotationEditorDividerGap / 2;
  x += AnnotationEditorDividerGap - AnnotationEditorButtonGap;

  const int action_total =
      AnnotationEditorButtonWidth * 2 + AnnotationEditorButtonGap;
  const int confirm_x =
      (std::max)(x, client_width - action_total - AnnotationEditorBarPadding);

  data->toolbar_items[item_index] = {
      kButtonConfirmId,
      ToolbarIconKind::Confirm,
      true,
      {confirm_x, y, confirm_x + AnnotationEditorButtonWidth,
       y + AnnotationEditorButtonHeight}};
  ++item_index;

  const int cancel_x =
      confirm_x + AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  data->toolbar_items[item_index] = {
      kButtonCancelId,
      ToolbarIconKind::Cancel,
      false,
      {cancel_x, y, cancel_x + AnnotationEditorButtonWidth,
       y + AnnotationEditorButtonHeight}};

  data->tooltip = createToolbarTooltip(hwnd);
  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    bindToolbarTooltip(data->tooltip, hwnd, data->toolbar_items[i].id,
                       data->toolbar_items[i].rect,
                       toolbarIconLabel(data->toolbar_items[i].icon),
                       data->tooltip_text[i], kToolbarTooltipMaxChars);
  }
  if (data->font_combo != nullptr)
  {
    RECT combo_rect{};
    GetWindowRect(data->font_combo, &combo_rect);
    POINT top_left{combo_rect.left, combo_rect.top};
    POINT bottom_right{combo_rect.right, combo_rect.bottom};
    ScreenToClient(hwnd, &top_left);
    ScreenToClient(hwnd, &bottom_right);
    RECT local{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    bindToolbarTooltip(data->tooltip, hwnd, kFontComboId, local,
                       L"\x5B57\x53F7", data->tooltip_text[kToolbarIconItemCount],
                       kToolbarTooltipMaxChars);
  }

  return item_index + 1 == kToolbarIconItemCount;
}

void requestClose(EditorWindowData* data, bool confirmed)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return;
  }
  data->confirmed = confirmed;
  PostMessageW(data->overlay, WM_CLOSE, 0, 0);
}

void finishAndNotify(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }

  AnnotationFinishResult result;
  if (data->confirmed)
  {
    if (!data->session.finishConfirmed(result))
    {
      result.cancelled = true;
      result.rendered_image = Image{};
    }
  }
  else
  {
    (void)data->session.finishCancelled(result);
  }

  if (data->callback)
  {
    data->callback(result);
  }
}

bool pointInImageArea(const Image& source, int x, int y)
{
  return x >= 0 && y >= 0 && x < source.width && y < source.height;
}

void invalidateToolbar(EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return;
  }
  const Image& source = data->session.source();
  RECT rect{0, source.height, data->client_width,
            source.height + annotationEditorToolbarHeight()};
  InvalidateRect(data->overlay, &rect, FALSE);
}

int hitTestEditorToolbar(const EditorWindowData* data, int x, int y)
{
  if (data == nullptr)
  {
    return -1;
  }
  const POINT pt{x, y};
  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    if (PtInRect(&data->toolbar_items[i].rect, pt) != FALSE)
    {
      return i;
    }
  }
  return -1;
}

void handleToolbarItemClick(EditorWindowData* data, UINT id)
{
  if (data == nullptr)
  {
    return;
  }
  if (id == kButtonConfirmId)
  {
    commitInlineText(data);
    requestClose(data, true);
    return;
  }
  if (id == kButtonCancelId)
  {
    cancelInlineText(data);
    requestClose(data, false);
    return;
  }
  handleToolCommand(data, id);
  invalidateToolbar(data);
}

void handleToolCommand(EditorWindowData* data, UINT id)
{
  if (data == nullptr)
  {
    return;
  }

  if (id != kButtonTextId)
  {
    commitInlineText(data);
    clearTextSelection(data);
  }

  switch (id)
  {
    case kButtonRectId:
      data->controller.setTool(AnnotationTool::Rectangle);
      break;
    case kButtonEllipseId:
      data->controller.setTool(AnnotationTool::Ellipse);
      break;
    case kButtonArrowId:
      data->controller.setTool(AnnotationTool::Arrow);
      break;
    case kButtonPenId:
      data->controller.setTool(AnnotationTool::Pen);
      break;
    case kButtonMosaicId:
      data->controller.setTool(AnnotationTool::Mosaic);
      break;
    case kButtonTextId:
      data->controller.setTool(AnnotationTool::Text);
      break;
    case kButtonUndoId:
      cancelInlineText(data);
      resetTextGesture(data);
      clearTextSelection(data);
      if (data->controller.undo(data->session.engine()))
      {
        invalidateImageArea(data->overlay, data->session.source());
      }
      break;
    default:
      break;
  }
  invalidateToolbar(data);
}

LRESULT CALLBACK editorWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                               LPARAM lparam)
{
  EditorWindowData* data = reinterpret_cast<EditorWindowData*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));

  switch (msg)
  {
    case WM_NCCREATE:
    {
      const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
      EditorWindowData* create_data =
          static_cast<EditorWindowData*>(cs->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(create_data));
      if (create_data != nullptr)
      {
        create_data->overlay = hwnd;
      }
      return TRUE;
    }
    case WM_CREATE:
    {
      if (data == nullptr || !createButtons(hwnd, data))
      {
        return -1;
      }
      return 0;
    }
    case WM_PAINT:
    {
      const PaintGuard paint(hwnd);
      paintEditor(data, paint.dc());
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_LBUTTONDOWN:
    {
      if (data == nullptr)
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      if (!pointInImageArea(data->session.source(), x, y))
      {
        const int hit = hitTestEditorToolbar(data, x, y);
        if (hit >= 0)
        {
          handleToolbarItemClick(data, data->toolbar_items[hit].id);
        }
        return 0;
      }
      if (data->controller.tool() == AnnotationTool::Text)
      {
        beginOrEditTextAt(data, hwnd, x, y);
        return 0;
      }
      clearTextSelection(data);
      if (data->controller.beginStroke(static_cast<float>(x),
                                       static_cast<float>(y)))
      {
        SetCapture(hwnd);
        invalidateImageArea(hwnd, data->session.source());
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
      if (data->controller.tool() != AnnotationTool::Text ||
          !pointInImageArea(data->session.source(), x, y))
      {
        return 0;
      }
      commitInlineText(data);
      resetTextGesture(data);
      ReleaseCapture();
      const std::size_t hit = hitTestTextAnnotation(data, x, y);
      if (hit != kInvalidAnnotationIndex)
      {
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
      if (!pointInImageArea(data->session.source(), x, y) &&
          !data->controller.isDrawing() && !data->text_gesture_active)
      {
        const int hit = hitTestEditorToolbar(data, x, y);
        if (hit != data->toolbar_hover)
        {
          data->toolbar_hover = hit;
          invalidateToolbar(data);
        }
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        TrackMouseEvent(&track);
        return 0;
      }
      if (data->toolbar_hover >= 0)
      {
        data->toolbar_hover = -1;
        invalidateToolbar(data);
      }
      if (data->text_gesture_active)
      {
        updateTextGesture(data, x, y);
        return 0;
      }
      if (!data->controller.isDrawing())
      {
        return 0;
      }
      data->controller.updateStroke(static_cast<float>(x),
                                    static_cast<float>(y));
      invalidateImageArea(hwnd, data->session.source());
      return 0;
    }
    case WM_MOUSELEAVE:
      if (data != nullptr && data->toolbar_hover >= 0)
      {
        data->toolbar_hover = -1;
        invalidateToolbar(data);
      }
      return 0;
    case WM_LBUTTONUP:
    {
      if (data == nullptr)
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      if (data->text_gesture_active)
      {
        finishTextGesture(data, hwnd, x, y);
        return 0;
      }
      if (!data->controller.isDrawing())
      {
        return 0;
      }
      data->controller.updateStroke(static_cast<float>(x),
                                    static_cast<float>(y));
      (void)data->controller.endStroke(data->session.engine());
      ReleaseCapture();
      invalidateImageArea(hwnd, data->session.source());
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
      if (id == kFontComboId && code == CBN_SELCHANGE)
      {
        syncFontSizeFromCombo(data);
        return 0;
      }
      if (id == kInlineEditId && code == EN_KILLFOCUS)
      {
        const HWND focus = GetFocus();
        if (focus == data->font_combo)
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
    case WM_CLOSE:
      commitInlineText(data);
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      if (data != nullptr && data->tooltip != nullptr)
      {
        DestroyWindow(data->tooltip);
        data->tooltip = nullptr;
      }
      destroyInlineEdit(data);
      finishAndNotify(data);
      if (data->loop_done != nullptr)
      {
        *data->loop_done = true;
      }
      return 0;
    default:
      break;
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
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = kOverlayClassName;

  return RegisterClassExW(&wc) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool isCtrlZ(const MSG& msg)
{
  if (msg.message != WM_KEYDOWN || msg.wParam != 'Z')
  {
    return false;
  }
  return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

}  // namespace

bool AnnotationOverlay::show(HWND owner, const Image& source,
                             AnnotationCallback callback)
{
  return showInPlace(owner, source, -1, -1, std::move(callback));
}

bool AnnotationOverlay::showInPlace(HWND owner, const Image& source,
                                    int screen_x, int screen_y,
                                    AnnotationCallback callback)
{
  if (m_visible)
  {
    return false;
  }

  EditorWindowData data;
  if (!data.session.begin(source))
  {
    return false;
  }
  data.client_width = annotationEditorClientWidth(source.width);
  data.controller.setCanvasSize(source.width, source.height);
  data.controller.setTool(AnnotationTool::Rectangle);
  data.callback = std::move(callback);

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerEditorClass(instance))
  {
    return false;
  }

  const int window_width = data.client_width;
  const int window_height =
      source.height + annotationEditorToolbarHeight();
  int x = 0;
  int y = 0;
  if (screen_x < 0 || screen_y < 0)
  {
    x = (std::max)(0, (GetSystemMetrics(SM_CXSCREEN) - window_width) / 2);
    y = (std::max)(0, (GetSystemMetrics(SM_CYSCREEN) - window_height) / 2);
  }
  else
  {
    // 就地编辑：图片原点钉死在选区左上角，禁止为塞工具栏而夹屏平移。
    const AnnotationEditorInPlacePlacement place =
        annotationEditorInPlacePlacement(screen_x, screen_y, source.width,
                                         source.height);
    x = place.window_x;
    y = place.window_y;
  }

  bool done = false;
  data.loop_done = &done;

  const DWORD style = WS_POPUP | WS_VISIBLE;
  const HWND hwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClassName, L"", style, x, y,
      window_width, window_height, owner, nullptr, instance, &data);
  if (hwnd == nullptr)
  {
    return false;
  }

  m_hwnd = hwnd;
  m_visible = true;

  ShowWindow(hwnd, SW_SHOW);
  SetForegroundWindow(hwnd);

  MSG msg{};
  while (!done && GetMessageW(&msg, nullptr, 0, 0) > 0)
  {
    if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE)
    {
      if (data.inline_edit != nullptr)
      {
        cancelInlineText(&data);
        continue;
      }
      if (data.text_gesture_active)
      {
        resetTextGesture(&data);
        ReleaseCapture();
        invalidateImageArea(hwnd, data.session.source());
        continue;
      }
      if (data.selected_text_index != kInvalidAnnotationIndex)
      {
        clearTextSelection(&data);
        invalidateImageArea(hwnd, data.session.source());
        continue;
      }
      requestClose(&data, false);
      continue;
    }
    if (msg.message == WM_KEYDOWN &&
        (msg.wParam == VK_DELETE || msg.wParam == VK_BACK) &&
        data.inline_edit == nullptr)
    {
      if (deleteSelectedTextAnnotation(&data))
      {
        continue;
      }
    }
    if (isCtrlZ(msg))
    {
      if (data.inline_edit != nullptr)
      {
        cancelInlineText(&data);
      }
      clearTextSelection(&data);
      if (data.controller.undo(data.session.engine()))
      {
        invalidateImageArea(hwnd, data.session.source());
      }
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  m_visible = false;
  m_hwnd = nullptr;
  return true;
}

void AnnotationOverlay::hide()
{
  if (m_hwnd != nullptr)
  {
    PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
  }
}

bool AnnotationOverlay::isVisible() const
{
  return m_visible;
}

}  // namespace qingying
