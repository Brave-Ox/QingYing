#include "qingying/annotate/annotation_overlay.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

#include <commctrl.h>

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
constexpr UINT kButtonGeometryId = 23;
constexpr UINT kInlineEditId = 10;
constexpr UINT kFontComboId = 20;
constexpr UINT kTipShapeRectId = 200;
constexpr UINT kTipShapeEllipseId = 201;
constexpr UINT kTipFillId = 202;
constexpr UINT kTipLineStyleBaseId = 210;
constexpr UINT kTipStrokeBaseId = 220;
constexpr UINT kTipColorBaseId = 230;
constexpr int kComboFontPx = 13;
const wchar_t kUiFontFace[] = L"Microsoft YaHei UI";

constexpr int kInlineTextMaxChars = 256;
constexpr int kTextDragThresholdPx = 4;
constexpr int kTextHitPaddingPx = 10;
constexpr int kTextMinHitWidthPx = 28;
constexpr int kTextMinHitHeightPx = 20;
constexpr std::size_t kInvalidAnnotationIndex =
    static_cast<std::size_t>(-1);

constexpr int kToolbarIconItemCount = 8;
constexpr int kGeometryShapeCount = 2;
constexpr int kComboTooltipSlot = kToolbarIconItemCount;
constexpr int kShapeTooltipSlot = kComboTooltipSlot + 1;
constexpr int kFillTooltipSlot = kShapeTooltipSlot + kGeometryShapeCount;
constexpr int kLineStyleTooltipSlot = kFillTooltipSlot + 1;
constexpr int kStrokeTooltipSlot =
    kLineStyleTooltipSlot + AnnotationLineStyleCount;
constexpr int kColorTooltipSlot =
    kStrokeTooltipSlot + AnnotationStylePresetStrokeCount;
constexpr int kTooltipSlotCount =
    kColorTooltipSlot + AnnotationStylePresetColorCount;
constexpr int kStrokePreviewInsetPx = 6;
constexpr int kSwatchCornerRadius = 4;
constexpr COLORREF kSwatchBorderColor = RGB(160, 164, 170);
constexpr COLORREF kSwatchSelectedBorderColor = RGB(40, 44, 52);
constexpr COLORREF kFrameBorderColor = RGB(255, 128, 0);
constexpr COLORREF kFrameChromeFill = RGB(32, 32, 32);
constexpr COLORREF kHandleFillColor = RGB(255, 255, 255);
constexpr COLORREF kSizeLabelFillColor = RGB(60, 64, 70);
constexpr COLORREF kSizeLabelTextColor = RGB(255, 255, 255);
constexpr COLORREF kInlineEditBorderColor = RGB(0, 0, 0);
constexpr COLORREF kTextChromeBorderColor = RGB(0, 0, 0);
constexpr COLORREF kTextDeleteFillColor = RGB(220, 56, 48);
constexpr COLORREF kTextDeleteGlyphColor = RGB(255, 255, 255);
constexpr int kTextDeleteGlyphWidthPx = 2;
constexpr int kTextDeleteGlyphInsetPx = 4;
constexpr int kSizeLabelFontPx = 12;
constexpr int kSizeLabelPadX = 6;
constexpr int kSizeLabelPadY = 2;
constexpr UINT kMsgCancelFromBackdrop = WM_APP + 2;
const wchar_t kEditorHwndPropName[] = L"QingYingAnnotationHwnd";

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
  HFONT combo_font{nullptr};
  bool size_combo_syncing{false};
  HWND inline_edit{nullptr};
  WNDPROC inline_edit_prev_proc{nullptr};
  HFONT inline_edit_font{nullptr};
  float text_anchor_x{0.0f};
  float text_anchor_y{0.0f};
  std::size_t editing_text_index{kInvalidAnnotationIndex};
  std::size_t selected_text_index{kInvalidAnnotationIndex};
  DWORD last_text_click_tick{0};
  std::size_t last_text_click_index{kInvalidAnnotationIndex};
  int last_text_click_x{0};
  int last_text_click_y{0};
  bool inline_edit_pressing{false};
  int inline_edit_press_x{0};
  int inline_edit_press_y{0};
  bool inline_commit_busy{false};
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
  RECT color_swatch_rects[AnnotationStylePresetColorCount]{};
  RECT stroke_rects[AnnotationStylePresetStrokeCount]{};
  RECT shape_rects[kGeometryShapeCount]{};
  RECT fill_rect{};
  RECT line_style_rects[AnnotationLineStyleCount]{};
  AnnotationTool last_geometry_tool{AnnotationTool::Rectangle};
  int toolbar_hover{-1};
  int toolbar_divider_x[AnnotationEditorDividerCount]{};
  HWND tooltip{nullptr};
  wchar_t tooltip_text[kTooltipSlotCount][kToolbarTooltipMaxChars]{};
  bool confirmed{false};
  int client_width{0};
  bool* loop_done{nullptr};
  int image_origin_x{AnnotationEditorFrameInsetPx};
  int image_origin_y{0};
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

void invalidateImageArea(const EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return;
  }
  const Image& source = data->session.source();
  if (source.empty())
  {
    return;
  }
  RECT rect{data->image_origin_x, data->image_origin_y,
            data->image_origin_x + source.width,
            data->image_origin_y + source.height};
  (void)InflateRect(&rect, AnnotationEditorTextDeleteButtonPx,
                    AnnotationEditorTextDeleteButtonPx);
  InvalidateRect(data->overlay, &rect, FALSE);
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
void paintEditor(EditorWindowData* data, HDC hdc);
void paintEditorBuffered(EditorWindowData* data, HDC hdc);
void paintInlineEditFrame(HDC hdc, EditorWindowData* data);
void layoutInlineEdit(EditorWindowData* data);
void applyInlineEditVisual(EditorWindowData* data);
void paintEditorToolbar(HDC hdc, EditorWindowData* data);
void canvasFromClient(const EditorWindowData* data, int x, int y, float& out_x,
                      float& out_y);
void paintPropertyBar(HDC hdc, EditorWindowData* data);
void layoutPropertyBar(HWND hwnd, EditorWindowData* data);
void resizeEditorChrome(EditorWindowData* data);
void syncGeometryButton(EditorWindowData* data);
int hitTestShapeToggle(const EditorWindowData* data, int x, int y);
bool hitTestFill(const EditorWindowData* data, int x, int y);
int hitTestLineStyle(const EditorWindowData* data, int x, int y);
void applyLiveTextStyle(EditorWindowData* data);
void syncStyleFromAnnotation(EditorWindowData* data,
                             const Annotation& annotation);
int hitTestColorSwatch(const EditorWindowData* data, int x, int y);
int hitTestStrokePreset(const EditorWindowData* data, int x, int y);
void handlePropertyBarClick(EditorWindowData* data, int x, int y);
void invalidateToolbar(EditorWindowData* data);
int hitTestEditorToolbar(const EditorWindowData* data, int x, int y);
void handleToolCommand(EditorWindowData* data, UINT id);
void handleToolbarItemClick(EditorWindowData* data, UINT id);
bool createButtons(HWND hwnd, EditorWindowData* data);
void fillSizeCombo(EditorWindowData* data);
void syncSizeFromCombo(EditorWindowData* data);
void bindSizeComboTooltip(EditorWindowData* data);
void bindPropertyBarTooltips(EditorWindowData* data);
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
    const HWND edit = data->inline_edit;
    // 必须先断开成员：DestroyWindow 会同步派发 EN_KILLFOCUS，
    // 否则 commitInlineText 会再次 add 同一条新文字。
    data->inline_edit = nullptr;
    if (GetCapture() == edit)
    {
      ReleaseCapture();
    }
    if (data->inline_edit_prev_proc != nullptr)
    {
      SetWindowLongPtrW(edit, GWLP_WNDPROC,
                        reinterpret_cast<LONG_PTR>(data->inline_edit_prev_proc));
      data->inline_edit_prev_proc = nullptr;
    }
    DestroyWindow(edit);
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

  const AnnotationEditorInlineCommitGuard guard(data->inline_commit_busy);
  if (!guard.acquired())
  {
    return;
  }

  wchar_t buffer[kInlineTextMaxChars]{};
  GetWindowTextW(data->inline_edit, buffer, kInlineTextMaxChars);
  const float anchor_x = data->text_anchor_x;
  const float anchor_y = data->text_anchor_y;
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
  annotation.style = data->controller.style();
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
    invalidateImageArea(data);
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
  if (index < data->session.engine().document().count())
  {
    syncStyleFromAnnotation(
        data, data->session.engine().document().items().at(index));
  }
  SetFocus(hwnd);
  invalidateImageArea(data);
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
  invalidateImageArea(data);
  return true;
}

void selectFontSizeInCombo(EditorWindowData* data, int font_size)
{
  if (data == nullptr)
  {
    return;
  }

  data->controller.setFontSize(font_size);
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
    out_width = (std::max)(static_cast<int>(size.cx), kTextMinHitWidthPx);
    out_height = (std::max)(static_cast<int>(size.cy), kTextMinHitHeightPx);
  }
}

AnnotationEditorTextChrome makeTextChrome(const EditorWindowData* data,
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
  const HDC hdc = GetDC(data->overlay);
  if (hdc != nullptr)
  {
    measureTextHitSize(hdc, annotation, width, height);
    ReleaseDC(data->overlay, hdc);
  }

  int text_x = static_cast<int>(annotation.start.x);
  int text_y = static_cast<int>(annotation.start.y);
  if (use_drag_position)
  {
    text_x = static_cast<int>(data->text_drag_x);
    text_y = static_cast<int>(data->text_drag_y);
  }
  return annotationEditorTextChrome(data->image_origin_x, data->image_origin_y,
                                    text_x, text_y, width, height);
}

std::size_t hitTestTextAnnotation(EditorWindowData* data, int x, int y)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return kInvalidAnnotationIndex;
  }

  const auto& items = data->session.engine().document().items();
  for (std::size_t i = items.size(); i > 0; --i)
  {
    const std::size_t index = i - 1;
    const Annotation& annotation = items.at(index);
    if (annotation.type != AnnotationType::Text)
    {
      continue;
    }

    const bool dragging =
        data->text_dragging && data->text_target_index == index;
    const AnnotationEditorTextChrome chrome =
        makeTextChrome(data, annotation, dragging);
    AnnotationEditorRect grab = chrome.frame;
    grab.left -= kTextHitPaddingPx;
    grab.top -= kTextHitPaddingPx;
    grab.right += kTextHitPaddingPx;
    grab.bottom += kTextHitPaddingPx;
    if (annotationEditorContains(chrome.delete_button, x, y) ||
        annotationEditorContains(grab, x, y))
    {
      return index;
    }
  }
  return kInvalidAnnotationIndex;
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
    updated.style.font_size = data->controller.style().font_size;
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
  invalidateImageArea(data);
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
    else if (msg == WM_ERASEBKGND)
    {
      return 1;
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

void invalidateInlineEditRegion(const EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr ||
      data->inline_edit == nullptr)
  {
    return;
  }

  RECT rect{};
  if (GetWindowRect(data->inline_edit, &rect) == FALSE)
  {
    return;
  }
  MapWindowPoints(HWND_DESKTOP, data->overlay, reinterpret_cast<POINT*>(&rect),
                  2);
  const int pad = AnnotationEditorInlineEditBorderPx + 1;
  (void)InflateRect(&rect, pad, pad);
  InvalidateRect(data->overlay, &rect, FALSE);
}

void layoutInlineEdit(EditorWindowData* data)
{
  if (data == nullptr || data->inline_edit == nullptr)
  {
    return;
  }

  const Image& source = data->session.source();
  const int canvas_x = static_cast<int>(data->text_anchor_x);
  const int canvas_y = static_cast<int>(data->text_anchor_y);
  const int remain_width = source.width - canvas_x;

  wchar_t buffer[kInlineTextMaxChars]{};
  GetWindowTextW(data->inline_edit, buffer, kInlineTextMaxChars);

  int text_extent = 0;
  const HDC hdc = GetDC(data->inline_edit);
  if (hdc != nullptr)
  {
    HGDIOBJ old_font = nullptr;
    if (data->inline_edit_font != nullptr)
    {
      old_font = SelectObject(hdc, data->inline_edit_font);
    }
    const int len = lstrlenW(buffer);
    SIZE size{};
    if (len > 0)
    {
      (void)GetTextExtentPoint32W(hdc, buffer, len, &size);
      text_extent = static_cast<int>(size.cx);
    }
    if (old_font != nullptr)
    {
      SelectObject(hdc, old_font);
    }
    ReleaseDC(data->inline_edit, hdc);
  }

  invalidateInlineEditRegion(data);
  const int width = annotationEditorInlineEditWidth(text_extent, remain_width);
  const int height =
      annotationEditorInlineEditHeight(data->controller.style().font_size);
  SetWindowPos(data->inline_edit, nullptr, data->image_origin_x + canvas_x,
               data->image_origin_y + canvas_y, width, height,
               SWP_NOZORDER | SWP_NOACTIVATE);
  invalidateInlineEditRegion(data);
}

void applyInlineEditVisual(EditorWindowData* data)
{
  if (data == nullptr || data->inline_edit == nullptr)
  {
    return;
  }
  InvalidateRect(data->inline_edit, nullptr, TRUE);
  layoutInlineEdit(data);
}

void paintInlineEditFrame(HDC hdc, EditorWindowData* data)
{
  if (hdc == nullptr || data == nullptr || data->inline_edit == nullptr)
  {
    return;
  }

  RECT rect{};
  if (GetWindowRect(data->inline_edit, &rect) == FALSE)
  {
    return;
  }
  MapWindowPoints(HWND_DESKTOP, data->overlay, reinterpret_cast<POINT*>(&rect),
                  2);
  const int border = AnnotationEditorInlineEditBorderPx;
  const HPEN pen = CreatePen(PS_SOLID, border, kInlineEditBorderColor);
  if (pen == nullptr)
  {
    return;
  }
  const HGDIOBJ old_pen = SelectObject(hdc, pen);
  const HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, rect.left - border, rect.top - border, rect.right + border,
            rect.bottom + border);
  SelectObject(hdc, old_brush);
  SelectObject(hdc, old_pen);
  DeleteObject(pen);
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
  float click_x = 0.0f;
  float click_y = 0.0f;
  canvasFromClient(data, x, y, click_x, click_y);
  data->text_anchor_x = click_x;
  data->text_anchor_y = click_y;

  if (edit_index != kInvalidAnnotationIndex &&
      edit_index < data->session.engine().document().count())
  {
    const Annotation& existing =
        data->session.engine().document().items().at(edit_index);
    data->text_anchor_x = existing.start.x;
    data->text_anchor_y = existing.start.y;
    initial_text = existing.text;
    selectFontSizeInCombo(data, existing.style.font_size);
    data->controller.setColor(existing.style.color);
  }

  const int canvas_x = static_cast<int>(data->text_anchor_x);
  const int canvas_y = static_cast<int>(data->text_anchor_y);
  const int edit_x = data->image_origin_x + canvas_x;
  const int edit_y = data->image_origin_y + canvas_y;
  const int remain_width = source.width - canvas_x;
  const int edit_width = annotationEditorInlineEditWidth(0, remain_width);
  const int edit_height =
      annotationEditorInlineEditHeight(data->controller.style().font_size);

  data->inline_edit = CreateWindowExW(
      0, L"EDIT", initial_text.c_str(),
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
      -data->controller.style().font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
      FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
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
  layoutInlineEdit(data);
  SetFocus(data->inline_edit);
  invalidateImageArea(data);
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
    const AnnotationEditorTextChrome chrome =
        makeTextChrome(data, existing, false);
    if (annotationEditorHitTextChrome(chrome, x, y) ==
        AnnotationEditorTextHit::Delete)
    {
      selectTextAnnotation(data, hwnd, hit, x, y);
      (void)deleteSelectedTextAnnotation(data);
      return;
    }

    selectTextAnnotation(data, hwnd, hit, x, y);
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
  invalidateImageArea(data);
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

void fillSizeCombo(EditorWindowData* data)
{
  if (data == nullptr || data->font_combo == nullptr)
  {
    return;
  }

  const bool mosaic =
      annotationEditorPropertyBarShowsMosaicSize(data->controller.tool());
  const int* options = mosaic ? AnnotationEditorMosaicSizeOptions
                              : AnnotationEditorFontSizeOptions;
  const int count = mosaic ? AnnotationEditorMosaicSizeOptionCount
                           : AnnotationEditorFontSizeOptionCount;
  const int current = mosaic ? data->controller.mosaicBlockSize()
                             : data->controller.style().font_size;

  data->size_combo_syncing = true;
  SendMessageW(data->font_combo, CB_RESETCONTENT, 0, 0);
  int selected = 0;
  for (int i = 0; i < count; ++i)
  {
    wchar_t label[16]{};
    swprintf_s(label, L"%d", options[i]);
    SendMessageW(data->font_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(label));
    if (options[i] == current)
    {
      selected = i;
    }
  }
  SendMessageW(data->font_combo, CB_SETCURSEL, static_cast<WPARAM>(selected),
               0);
  data->size_combo_syncing = false;
}

void bindSizeComboTooltip(EditorWindowData* data)
{
  if (data == nullptr || data->tooltip == nullptr || data->overlay == nullptr)
  {
    return;
  }

  RECT local{};
  const bool show_combo =
      data->font_combo != nullptr &&
      annotationEditorPropertyBarShowsSizeCombo(data->controller.tool());
  if (show_combo)
  {
    RECT combo_rect{};
    GetWindowRect(data->font_combo, &combo_rect);
    POINT top_left{combo_rect.left, combo_rect.top};
    POINT bottom_right{combo_rect.right, combo_rect.bottom};
    ScreenToClient(data->overlay, &top_left);
    ScreenToClient(data->overlay, &bottom_right);
    local = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  }

  const wchar_t* tip = L"";
  if (show_combo)
  {
    tip = annotationEditorPropertyBarShowsMosaicSize(data->controller.tool())
              ? L"\x5757\x5927\x5C0F"
              : L"\x5B57\x53F7";
  }
  bindToolbarTooltip(data->tooltip, data->overlay, kFontComboId, local, tip,
                     data->tooltip_text[kComboTooltipSlot],
                     kToolbarTooltipMaxChars);
}

void bindPropertyBarTooltips(EditorWindowData* data)
{
  if (data == nullptr || data->tooltip == nullptr || data->overlay == nullptr)
  {
    return;
  }

  const HWND overlay = data->overlay;
  const HWND tooltip = data->tooltip;
  bindToolbarTooltip(tooltip, overlay, kTipShapeRectId, data->shape_rects[0],
                     toolbarIconLabel(ToolbarIconKind::Rectangle),
                     data->tooltip_text[kShapeTooltipSlot],
                     kToolbarTooltipMaxChars);
  bindToolbarTooltip(tooltip, overlay, kTipShapeEllipseId, data->shape_rects[1],
                     toolbarIconLabel(ToolbarIconKind::Ellipse),
                     data->tooltip_text[kShapeTooltipSlot + 1],
                     kToolbarTooltipMaxChars);
  bindToolbarTooltip(tooltip, overlay, kTipFillId, data->fill_rect,
                     toolbarIconLabel(ToolbarIconKind::Fill),
                     data->tooltip_text[kFillTooltipSlot],
                     kToolbarTooltipMaxChars);

  const ToolbarIconKind line_icons[AnnotationLineStyleCount] = {
      ToolbarIconKind::LineSolid, ToolbarIconKind::LineDashed,
      ToolbarIconKind::LineDotted};
  for (int i = 0; i < AnnotationLineStyleCount; ++i)
  {
    bindToolbarTooltip(
        tooltip, overlay, kTipLineStyleBaseId + static_cast<UINT>(i),
        data->line_style_rects[static_cast<std::size_t>(i)],
        toolbarIconLabel(line_icons[static_cast<std::size_t>(i)]),
        data->tooltip_text[kLineStyleTooltipSlot + i],
        kToolbarTooltipMaxChars);
  }

  for (int i = 0; i < AnnotationStylePresetStrokeCount; ++i)
  {
    bindToolbarTooltip(tooltip, overlay,
                       kTipStrokeBaseId + static_cast<UINT>(i),
                       data->stroke_rects[static_cast<std::size_t>(i)],
                       toolbarStrokePresetLabel(i),
                       data->tooltip_text[kStrokeTooltipSlot + i],
                       kToolbarTooltipMaxChars);
  }

  for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
  {
    bindToolbarTooltip(tooltip, overlay, kTipColorBaseId + static_cast<UINT>(i),
                       data->color_swatch_rects[static_cast<std::size_t>(i)],
                       toolbarColorPresetLabel(i),
                       data->tooltip_text[kColorTooltipSlot + i],
                       kToolbarTooltipMaxChars);
  }

  bindSizeComboTooltip(data);
}

void syncSizeFromCombo(EditorWindowData* data)
{
  if (data == nullptr || data->font_combo == nullptr ||
      data->size_combo_syncing)
  {
    return;
  }

  const LRESULT index = SendMessageW(data->font_combo, CB_GETCURSEL, 0, 0);
  if (index == CB_ERR || index < 0)
  {
    return;
  }

  if (annotationEditorPropertyBarShowsMosaicSize(data->controller.tool()))
  {
    if (index >= AnnotationEditorMosaicSizeOptionCount)
    {
      return;
    }
    data->controller.setMosaicBlockSize(
        AnnotationEditorMosaicSizeOptions[static_cast<std::size_t>(index)]);
    if (data->controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    return;
  }

  if (index >= AnnotationEditorFontSizeOptionCount)
  {
    return;
  }

  data->controller.setFontSize(
      AnnotationEditorFontSizeOptions[static_cast<std::size_t>(index)]);
  if (data->inline_edit != nullptr)
  {
    if (data->inline_edit_font != nullptr)
    {
      DeleteObject(data->inline_edit_font);
      data->inline_edit_font = nullptr;
    }
    data->inline_edit_font = CreateFontW(
        -data->controller.style().font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, kUiFontFace);
    if (data->inline_edit_font != nullptr)
    {
      SendMessageW(data->inline_edit, WM_SETFONT,
                   reinterpret_cast<WPARAM>(data->inline_edit_font), TRUE);
    }
    applyInlineEditVisual(data);
  }
  if (data->inline_edit != nullptr)
  {
    SetFocus(data->inline_edit);
  }
  applyLiveTextStyle(data);
}

void paintEditorFrame(HDC hdc, EditorWindowData* data)
{
  if (hdc == nullptr || data == nullptr)
  {
    return;
  }

  const Image& source = data->session.source();
  const int origin_x = data->image_origin_x;
  const int origin_y = data->image_origin_y;
  const int image_right = origin_x + source.width;
  const int image_bottom = origin_y + source.height;

  const int border = AnnotationEditorFrameBorderPx;
  const HPEN border_pen = CreatePen(PS_SOLID, border, kFrameBorderColor);
  if (border_pen != nullptr)
  {
    const HGDIOBJ old_pen = SelectObject(hdc, border_pen);
    const HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    const int half = border / 2;
    Rectangle(hdc, origin_x - half, origin_y - half, image_right + half + 1,
              image_bottom + half + 1);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(border_pen);
  }

  AnnotationEditorHandlePoint handles[AnnotationEditorHandleCount]{};
  annotationEditorHandlePoints(origin_x, origin_y, source.width, source.height,
                               handles);
  const int radius = AnnotationEditorHandleRadiusPx;
  const HPEN handle_pen = CreatePen(PS_SOLID, 1, kFrameBorderColor);
  const HBRUSH handle_brush = CreateSolidBrush(kHandleFillColor);
  if (handle_pen != nullptr && handle_brush != nullptr)
  {
    const HGDIOBJ old_pen = SelectObject(hdc, handle_pen);
    const HGDIOBJ old_brush = SelectObject(hdc, handle_brush);
    for (int i = 0; i < AnnotationEditorHandleCount; ++i)
    {
      const AnnotationEditorHandlePoint& point =
          handles[static_cast<std::size_t>(i)];
      Ellipse(hdc, point.x - radius, point.y - radius, point.x + radius + 1,
              point.y + radius + 1);
    }
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
  }
  if (handle_pen != nullptr)
  {
    DeleteObject(handle_pen);
  }
  if (handle_brush != nullptr)
  {
    DeleteObject(handle_brush);
  }

  wchar_t label[32]{};
  if (swprintf_s(label, L"%d x %d px", source.width, source.height) <= 0)
  {
    return;
  }

  HFONT font = CreateFontW(
      -kSizeLabelFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  if (font == nullptr)
  {
    return;
  }

  const HGDIOBJ old_font = SelectObject(hdc, font);
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
  const HBRUSH label_brush = CreateSolidBrush(kSizeLabelFillColor);
  if (label_brush != nullptr)
  {
    FillRect(hdc, &label_rect, label_brush);
    DeleteObject(label_brush);
  }
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, kSizeLabelTextColor);
  RECT text_rect = label_rect;
  text_rect.left += kSizeLabelPadX;
  text_rect.top += kSizeLabelPadY;
  DrawTextW(hdc, label, label_len, &text_rect,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  SelectObject(hdc, old_font);
  DeleteObject(font);
}

void paintEditor(EditorWindowData* data, HDC hdc)
{
  if (data == nullptr || hdc == nullptr)
  {
    return;
  }

  RECT client{};
  GetClientRect(data->overlay, &client);
  const HBRUSH chrome = CreateSolidBrush(kFrameChromeFill);
  if (chrome != nullptr)
  {
    FillRect(hdc, &client, chrome);
    DeleteObject(chrome);
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

  blitImage(hdc, composed, data->image_origin_x, data->image_origin_y);
  paintEditorFrame(hdc, data);
  paintInlineEditFrame(hdc, data);
  drawTextSelectionFrame(hdc, data);
  paintEditorToolbar(hdc, data);
  paintPropertyBar(hdc, data);
}

void paintEditorBuffered(EditorWindowData* data, HDC hdc)
{
  if (data == nullptr || hdc == nullptr || data->overlay == nullptr)
  {
    return;
  }

  RECT client{};
  GetClientRect(data->overlay, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width <= 0 || height <= 0)
  {
    return;
  }

  const HDC mem_dc = CreateCompatibleDC(hdc);
  if (mem_dc == nullptr)
  {
    paintEditor(data, hdc);
    return;
  }
  const HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
  if (bitmap == nullptr)
  {
    DeleteDC(mem_dc);
    paintEditor(data, hdc);
    return;
  }

  const HGDIOBJ old_bitmap = SelectObject(mem_dc, bitmap);
  paintEditor(data, mem_dc);
  (void)BitBlt(hdc, 0, 0, width, height, mem_dc, 0, 0, SRCCOPY);
  SelectObject(mem_dc, old_bitmap);
  DeleteObject(bitmap);
  DeleteDC(mem_dc);
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

  const bool dragging =
      data->text_dragging &&
      data->text_target_index == data->selected_text_index;
  const AnnotationEditorTextChrome chrome =
      makeTextChrome(data, annotation, dragging);

  const HPEN pen = CreatePen(PS_SOLID, AnnotationEditorInlineEditBorderPx,
                             kTextChromeBorderColor);
  if (pen != nullptr)
  {
    const HGDIOBJ old_pen = SelectObject(hdc, pen);
    const HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, chrome.frame.left, chrome.frame.top, chrome.frame.right,
              chrome.frame.bottom);
    SelectObject(hdc, old_brush);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
  }

  RECT delete_rect{chrome.delete_button.left, chrome.delete_button.top,
                   chrome.delete_button.right, chrome.delete_button.bottom};
  const HBRUSH delete_fill = CreateSolidBrush(kTextDeleteFillColor);
  if (delete_fill != nullptr)
  {
    FillRect(hdc, &delete_rect, delete_fill);
    DeleteObject(delete_fill);
  }

  const HPEN glyph = CreatePen(PS_SOLID, kTextDeleteGlyphWidthPx,
                               kTextDeleteGlyphColor);
  if (glyph == nullptr)
  {
    return;
  }
  const HGDIOBJ old_pen = SelectObject(hdc, glyph);
  const int inset = kTextDeleteGlyphInsetPx;
  MoveToEx(hdc, delete_rect.left + inset, delete_rect.top + inset, nullptr);
  LineTo(hdc, delete_rect.right - inset, delete_rect.bottom - inset);
  MoveToEx(hdc, delete_rect.right - inset, delete_rect.top + inset, nullptr);
  LineTo(hdc, delete_rect.left + inset, delete_rect.bottom - inset);
  SelectObject(hdc, old_pen);
  DeleteObject(glyph);
}

COLORREF colorBgraToRef(const ColorBgra& color)
{
  return static_cast<COLORREF>(annotationColorToRgb(color));
}

bool colorsMatch(const ColorBgra& left, const ColorBgra& right)
{
  return left.b == right.b && left.g == right.g && left.r == right.r &&
         left.a == right.a;
}

void syncStyleFromAnnotation(EditorWindowData* data,
                             const Annotation& annotation)
{
  if (data == nullptr)
  {
    return;
  }
  data->controller.setColor(annotation.style.color);
  selectFontSizeInCombo(data, annotation.style.font_size);
  invalidateToolbar(data);
}

void applyLiveTextStyle(EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return;
  }

  applyInlineEditVisual(data);

  const std::size_t index = data->selected_text_index;
  if (index == kInvalidAnnotationIndex ||
      index >= data->session.engine().document().count())
  {
    return;
  }
  if (data->inline_edit != nullptr && data->editing_text_index == index)
  {
    return;
  }

  Annotation updated = data->session.engine().document().items().at(index);
  if (updated.type != AnnotationType::Text)
  {
    return;
  }

  updated.style.color = data->controller.style().color;
  updated.style.font_size = data->controller.style().font_size;
  fillTextHitBounds(data->overlay, updated);
  if (data->session.engine().replaceAt(index, updated))
  {
    invalidateImageArea(data);
  }
}

RECT takeToolbarButtonRect(int& x, int y)
{
  RECT rect{x, y, x + AnnotationEditorButtonWidth,
            y + AnnotationEditorButtonHeight};
  x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  return rect;
}

void skipToolbarDivider(int& x)
{
  x += AnnotationEditorDividerGap - AnnotationEditorButtonGap;
}

void resetPropertyBarRects(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }

  for (int i = 0; i < kGeometryShapeCount; ++i)
  {
    data->shape_rects[static_cast<std::size_t>(i)] = {};
  }
  data->fill_rect = {};
  for (int i = 0; i < AnnotationLineStyleCount; ++i)
  {
    data->line_style_rects[static_cast<std::size_t>(i)] = {};
  }
  for (int i = 0; i < AnnotationStylePresetStrokeCount; ++i)
  {
    data->stroke_rects[static_cast<std::size_t>(i)] = {};
  }
  for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
  {
    data->color_swatch_rects[static_cast<std::size_t>(i)] = {};
  }
}

void syncGeometryButton(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }
  if (annotationEditorIsGeometryTool(data->controller.tool()))
  {
    data->last_geometry_tool = data->controller.tool();
  }
  data->toolbar_items[0].id = kButtonGeometryId;
  data->toolbar_items[0].icon =
      (data->last_geometry_tool == AnnotationTool::Ellipse)
          ? ToolbarIconKind::Ellipse
          : ToolbarIconKind::Rectangle;
}

void layoutPropertyBar(HWND hwnd, EditorWindowData* data)
{
  if (hwnd == nullptr || data == nullptr)
  {
    return;
  }

  const AnnotationTool tool = data->controller.tool();
  const Image& source = data->session.source();
  const int y = data->image_origin_y + source.height +
                annotationEditorToolbarHeight() + AnnotationEditorBarPadding;
  const int swatch = AnnotationEditorColorSwatchSize;
  const int swatch_y = y + (AnnotationEditorButtonHeight - swatch) / 2;
  int x = data->image_origin_x + AnnotationEditorBarPadding;
  const int mosaic_combo_x = x;
  resetPropertyBarRects(data);

  if (annotationEditorPropertyBarShowsShapeToggle(tool))
  {
    data->shape_rects[0] = takeToolbarButtonRect(x, y);
    data->shape_rects[1] = takeToolbarButtonRect(x, y);
    skipToolbarDivider(x);
  }
  if (annotationEditorPropertyBarShowsFill(tool))
  {
    data->fill_rect = takeToolbarButtonRect(x, y);
    skipToolbarDivider(x);
  }
  if (annotationEditorPropertyBarShowsLineStyle(tool))
  {
    for (int i = 0; i < AnnotationLineStyleCount; ++i)
    {
      data->line_style_rects[static_cast<std::size_t>(i)] =
          takeToolbarButtonRect(x, y);
    }
    skipToolbarDivider(x);
  }

  if (annotationEditorPropertyBarShowsStroke(tool) &&
      annotationEditorIsGeometryTool(tool))
  {
    for (int i = 0; i < AnnotationStylePresetStrokeCount; ++i)
    {
      data->stroke_rects[static_cast<std::size_t>(i)] =
          takeToolbarButtonRect(x, y);
    }
    skipToolbarDivider(x);
  }

  int combo_x = x;
  if (annotationEditorPropertyBarShowsColor(tool))
  {
    for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
    {
      data->color_swatch_rects[static_cast<std::size_t>(i)] = {
          x, swatch_y, x + swatch, swatch_y + swatch};
      x += swatch + AnnotationEditorButtonGap;
    }
    x += AnnotationEditorDividerGap - AnnotationEditorButtonGap;
    combo_x = x;
  }

  if (annotationEditorPropertyBarShowsStroke(tool) &&
      !annotationEditorIsGeometryTool(tool))
  {
    combo_x = x;
    for (int i = 0; i < AnnotationStylePresetStrokeCount; ++i)
    {
      data->stroke_rects[static_cast<std::size_t>(i)] =
          takeToolbarButtonRect(x, y);
    }
  }

  if (data->font_combo != nullptr)
  {
    const int placed_x =
        annotationEditorPropertyBarShowsMosaicSize(tool) ? mosaic_combo_x
                                                         : combo_x;
    SetWindowPos(data->font_combo, nullptr, placed_x, y,
                 AnnotationEditorFontComboWidth, AnnotationEditorButtonHeight,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }

  bindPropertyBarTooltips(data);
}

void resizeEditorChrome(EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return;
  }

  const AnnotationTool tool = data->controller.tool();
  const int height =
      annotationEditorWindowHeight(data->session.source().height, tool);
  SetWindowPos(data->overlay, nullptr, 0, 0, data->client_width, height,
               SWP_NOMOVE | SWP_NOZORDER);
  fillSizeCombo(data);
  if (data->font_combo != nullptr)
  {
    ShowWindow(data->font_combo,
               annotationEditorPropertyBarShowsSizeCombo(tool) ? SW_SHOW
                                                               : SW_HIDE);
  }
  layoutPropertyBar(data->overlay, data);
  syncGeometryButton(data);
  invalidateToolbar(data);
}

int hitTestColorSwatch(const EditorWindowData* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsColor(data->controller.tool()))
  {
    return -1;
  }

  const POINT pt{x, y};
  for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
  {
    if (PtInRect(&data->color_swatch_rects[static_cast<std::size_t>(i)], pt) !=
        FALSE)
    {
      return i;
    }
  }
  return -1;
}

int hitTestStrokePreset(const EditorWindowData* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsStroke(data->controller.tool()))
  {
    return -1;
  }

  const POINT pt{x, y};
  for (int i = 0; i < AnnotationStylePresetStrokeCount; ++i)
  {
    if (PtInRect(&data->stroke_rects[static_cast<std::size_t>(i)], pt) != FALSE)
    {
      return i;
    }
  }
  return -1;
}

int hitTestShapeToggle(const EditorWindowData* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsShapeToggle(data->controller.tool()))
  {
    return -1;
  }

  const POINT pt{x, y};
  for (int i = 0; i < kGeometryShapeCount; ++i)
  {
    if (PtInRect(&data->shape_rects[static_cast<std::size_t>(i)], pt) != FALSE)
    {
      return i;
    }
  }
  return -1;
}

bool hitTestFill(const EditorWindowData* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsFill(data->controller.tool()))
  {
    return false;
  }
  const POINT pt{x, y};
  return PtInRect(&data->fill_rect, pt) != FALSE;
}

int hitTestLineStyle(const EditorWindowData* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsLineStyle(data->controller.tool()))
  {
    return -1;
  }

  const POINT pt{x, y};
  for (int i = 0; i < AnnotationLineStyleCount; ++i)
  {
    if (PtInRect(&data->line_style_rects[static_cast<std::size_t>(i)], pt) !=
        FALSE)
    {
      return i;
    }
  }
  return -1;
}

void handlePropertyBarClick(EditorWindowData* data, int x, int y)
{
  if (data == nullptr)
  {
    return;
  }

  const int shape_index = hitTestShapeToggle(data, x, y);
  if (shape_index >= 0)
  {
    data->controller.setTool(shape_index == 0 ? AnnotationTool::Rectangle
                                              : AnnotationTool::Ellipse);
    syncGeometryButton(data);
    resizeEditorChrome(data);
    return;
  }

  if (hitTestFill(data, x, y))
  {
    data->controller.setFilled(!data->controller.style().filled);
    if (data->controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    invalidateToolbar(data);
    return;
  }

  const int line_index = hitTestLineStyle(data, x, y);
  if (line_index >= 0)
  {
    data->controller.setLineStyle(
        AnnotationLineStyleOptions[static_cast<std::size_t>(line_index)]);
    if (data->controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    invalidateToolbar(data);
    return;
  }

  const int color_index = hitTestColorSwatch(data, x, y);
  if (color_index >= 0)
  {
    data->controller.setColor(
        AnnotationStylePresetColors[static_cast<std::size_t>(color_index)]);
    applyLiveTextStyle(data);
    if (data->inline_edit != nullptr)
    {
      SetFocus(data->inline_edit);
    }
    if (data->controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    invalidateToolbar(data);
    return;
  }

  const int stroke_index = hitTestStrokePreset(data, x, y);
  if (stroke_index >= 0)
  {
    data->controller.setStrokeWidth(
        AnnotationStylePresetStrokeWidths[static_cast<std::size_t>(
            stroke_index)]);
    if (data->controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    invalidateToolbar(data);
  }
}

bool pointerHitsStyleChrome(const EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return false;
  }

  POINT pt{};
  if (GetCursorPos(&pt) == FALSE)
  {
    return false;
  }
  ScreenToClient(data->overlay, &pt);
  return hitTestEditorToolbar(data, pt.x, pt.y) >= 0 ||
         hitTestColorSwatch(data, pt.x, pt.y) >= 0 ||
         hitTestStrokePreset(data, pt.x, pt.y) >= 0 ||
         hitTestShapeToggle(data, pt.x, pt.y) >= 0 ||
         hitTestFill(data, pt.x, pt.y) ||
         hitTestLineStyle(data, pt.x, pt.y) >= 0;
}

void paintPropertyBar(HDC hdc, EditorWindowData* data)
{
  if (hdc == nullptr || data == nullptr ||
      !annotationEditorShowsPropertyBar(data->controller.tool()))
  {
    return;
  }

  const Image& source = data->session.source();
  const int bar_top =
      data->image_origin_y + source.height + annotationEditorToolbarHeight();
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

  const AnnotationTool tool = data->controller.tool();
  const AnnotationStyle& style = data->controller.style();

  if (annotationEditorPropertyBarShowsShapeToggle(tool))
  {
    drawToolbarItem(hdc, data->shape_rects[0], ToolbarIconKind::Rectangle,
                    false, tool == AnnotationTool::Rectangle, true, false);
    drawToolbarItem(hdc, data->shape_rects[1], ToolbarIconKind::Ellipse, false,
                    tool == AnnotationTool::Ellipse, true, false);
  }
  if (annotationEditorPropertyBarShowsFill(tool))
  {
    drawToolbarItem(hdc, data->fill_rect, ToolbarIconKind::Fill, false,
                    style.filled, true, false);
  }
  if (annotationEditorPropertyBarShowsLineStyle(tool))
  {
    const ToolbarIconKind line_icons[AnnotationLineStyleCount] = {
        ToolbarIconKind::LineSolid, ToolbarIconKind::LineDashed,
        ToolbarIconKind::LineDotted};
    for (int i = 0; i < AnnotationLineStyleCount; ++i)
    {
      const bool selected =
          style.line_style ==
          AnnotationLineStyleOptions[static_cast<std::size_t>(i)];
      drawToolbarItem(hdc, data->line_style_rects[static_cast<std::size_t>(i)],
                      line_icons[static_cast<std::size_t>(i)], false, selected,
                      true, false);
    }
  }

  if (annotationEditorPropertyBarShowsColor(tool))
  {
    for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
    {
      const ColorBgra& preset =
          AnnotationStylePresetColors[static_cast<std::size_t>(i)];
      const RECT& cell = data->color_swatch_rects[static_cast<std::size_t>(i)];
      const bool selected = colorsMatch(style.color, preset);
      fillRoundRect(hdc, cell, colorBgraToRef(preset),
                    selected ? kSwatchSelectedBorderColor : kSwatchBorderColor,
                    kSwatchCornerRadius);
    }
  }

  if (!annotationEditorPropertyBarShowsStroke(tool))
  {
    return;
  }

  const int divider_x =
      data->stroke_rects[0].left - AnnotationEditorDividerGap / 2;
  drawToolbarDivider(hdc, divider_x, bar.top + 8, bar.bottom - 8);

  for (int i = 0; i < AnnotationStylePresetStrokeCount; ++i)
  {
    const float width =
        AnnotationStylePresetStrokeWidths[static_cast<std::size_t>(i)];
    const RECT& cell = data->stroke_rects[static_cast<std::size_t>(i)];
    const bool selected = style.stroke_width == width;
    const COLORREF fill =
        selected ? colors.selected_fill : colors.button_fill;
    fillRoundRect(hdc, cell, fill, fill,
                  DefaultModernToolbarMetrics.hover_radius);

    const int thickness = (std::max)(1, static_cast<int>(width));
    const HPEN pen = CreatePen(PS_SOLID, thickness, colors.icon);
    if (pen == nullptr)
    {
      continue;
    }
    const HGDIOBJ old_pen = SelectObject(hdc, pen);
    const int mid_y = (cell.top + cell.bottom) / 2;
    MoveToEx(hdc, cell.left + kStrokePreviewInsetPx, mid_y, nullptr);
    LineTo(hdc, cell.right - kStrokePreviewInsetPx, mid_y);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
  }
}

void paintEditorToolbar(HDC hdc, EditorWindowData* data)
{
  if (hdc == nullptr || data == nullptr)
  {
    return;
  }

  const Image& source = data->session.source();
  const int bar_top = data->image_origin_y + source.height;
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
      case kButtonGeometryId:
        selected = annotationEditorIsGeometryTool(data->controller.tool());
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
                    selected, true, item.accent,
                    item.id == kButtonGeometryId);
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

  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_WIN95_CLASSES;
  (void)InitCommonControlsEx(&icc);

  const Image& source = data->session.source();
  const int client_width = data->client_width;
  const int y =
      data->image_origin_y + source.height + AnnotationEditorBarPadding;
  int x = data->image_origin_x + AnnotationEditorBarPadding;

  const struct
  {
    UINT id;
    ToolbarIconKind icon;
    bool accent;
  } left_items[] = {
      {kButtonGeometryId, ToolbarIconKind::Rectangle, false},
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

  data->font_combo = CreateWindowExW(
      0, L"COMBOBOX", L"",
      WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0,
      AnnotationEditorFontComboWidth, AnnotationEditorFontComboDropHeight, hwnd,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontComboId)),
      GetModuleHandleW(nullptr), nullptr);
  if (data->font_combo == nullptr)
  {
    return false;
  }

  data->combo_font = CreateFontW(
      -kComboFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, kUiFontFace);
  if (data->combo_font != nullptr)
  {
    SendMessageW(data->font_combo, WM_SETFONT,
                 reinterpret_cast<WPARAM>(data->combo_font), TRUE);
  }

  data->controller.setFontSize(DefaultFontSize);
  data->controller.setMosaicBlockSize(DefaultMosaicBlockSize);
  fillSizeCombo(data);

  layoutPropertyBar(hwnd, data);
  resizeEditorChrome(data);

  data->tooltip = createToolbarTooltip(hwnd);
  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    const wchar_t* tip = (data->toolbar_items[i].id == kButtonGeometryId)
                             ? toolbarIconLabel(ToolbarIconKind::Geometry)
                             : toolbarIconLabel(data->toolbar_items[i].icon);
    bindToolbarTooltip(data->tooltip, hwnd, data->toolbar_items[i].id,
                       data->toolbar_items[i].rect, tip, data->tooltip_text[i],
                       kToolbarTooltipMaxChars);
  }
  bindPropertyBarTooltips(data);

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

bool pointInImageArea(const EditorWindowData* data, int x, int y)
{
  if (data == nullptr)
  {
    return false;
  }
  const Image& source = data->session.source();
  const int local_x = x - data->image_origin_x;
  const int local_y = y - data->image_origin_y;
  return local_x >= 0 && local_y >= 0 && local_x < source.width &&
         local_y < source.height;
}

void canvasFromClient(const EditorWindowData* data, int x, int y, float& out_x,
                      float& out_y)
{
  out_x = 0.0f;
  out_y = 0.0f;
  if (data == nullptr)
  {
    return;
  }
  out_x = static_cast<float>(x - data->image_origin_x);
  out_y = static_cast<float>(y - data->image_origin_y);
}

void invalidateToolbar(EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return;
  }
  const Image& source = data->session.source();
  RECT rect{0, data->image_origin_y + source.height, data->client_width,
            data->image_origin_y + source.height +
                annotationEditorChromeHeight(data->controller.tool())};
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
    case kButtonGeometryId:
      data->controller.setTool(data->last_geometry_tool);
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
        invalidateImageArea(data);
      }
      break;
    default:
      break;
  }
  resizeEditorChrome(data);
  syncGeometryButton(data);
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
    case WM_CTLCOLOREDIT:
    {
      if (data == nullptr || data->inline_edit == nullptr)
      {
        break;
      }
      if (reinterpret_cast<HWND>(lparam) != data->inline_edit)
      {
        break;
      }
      const HDC hdc = reinterpret_cast<HDC>(wparam);
      SetTextColor(hdc, colorBgraToRef(data->controller.style().color));
      SetBkMode(hdc, TRANSPARENT);
      return reinterpret_cast<LRESULT>(GetStockObject(NULL_BRUSH));
    }
    case WM_LBUTTONDOWN:
    {
      if (data == nullptr)
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      if (data->controller.tool() == AnnotationTool::Text &&
          hitTestTextAnnotation(data, x, y) != kInvalidAnnotationIndex)
      {
        beginOrEditTextAt(data, hwnd, x, y);
        return 0;
      }
      if (!pointInImageArea(data, x, y))
      {
        const int hit = hitTestEditorToolbar(data, x, y);
        if (hit >= 0)
        {
          handleToolbarItemClick(data, data->toolbar_items[hit].id);
        }
        else
        {
          handlePropertyBarClick(data, x, y);
        }
        return 0;
      }
      if (data->controller.tool() == AnnotationTool::Text)
      {
        beginOrEditTextAt(data, hwnd, x, y);
        return 0;
      }
      clearTextSelection(data);
      float canvas_x = 0.0f;
      float canvas_y = 0.0f;
      canvasFromClient(data, x, y, canvas_x, canvas_y);
      if (data->controller.beginStroke(canvas_x, canvas_y))
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
      if (data->controller.tool() != AnnotationTool::Text ||
          !pointInImageArea(data, x, y))
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
      if (!pointInImageArea(data, x, y) &&
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
      float canvas_x = 0.0f;
      float canvas_y = 0.0f;
      canvasFromClient(data, x, y, canvas_x, canvas_y);
      data->controller.updateStroke(canvas_x, canvas_y);
      invalidateImageArea(data);
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
      float canvas_x = 0.0f;
      float canvas_y = 0.0f;
      canvasFromClient(data, x, y, canvas_x, canvas_y);
      data->controller.updateStroke(canvas_x, canvas_y);
      (void)data->controller.endStroke(data->session.engine());
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
      if (id == kFontComboId && code == CBN_SELCHANGE)
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
        if (focus == data->font_combo || pointerHitsStyleChrome(data))
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
        requestClose(data, false);
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
        if (data->tooltip != nullptr)
        {
          DestroyWindow(data->tooltip);
          data->tooltip = nullptr;
        }
        destroyInlineEdit(data);
        if (data->combo_font != nullptr)
        {
          DeleteObject(data->combo_font);
          data->combo_font = nullptr;
        }
        finishAndNotify(data);
        if (data->loop_done != nullptr)
        {
          *data->loop_done = true;
        }
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
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
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
  data.image_origin_x = AnnotationEditorFrameInsetPx;
  data.image_origin_y = annotationEditorTopInset();
  data.controller.setCanvasSize(source.width, source.height);
  data.controller.setTool(AnnotationTool::None);
  data.callback = std::move(callback);

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerEditorClass(instance))
  {
    return false;
  }

  int window_width = data.client_width;
  int window_height =
      annotationEditorWindowHeight(source.height, data.controller.tool());
  int x = 0;
  int y = 0;
  if (screen_x < 0 || screen_y < 0)
  {
    x = (std::max)(0, (GetSystemMetrics(SM_CXSCREEN) - window_width) / 2);
    y = (std::max)(0, (GetSystemMetrics(SM_CYSCREEN) - window_height) / 2);
  }
  else
  {
    // 就地编辑：图片原点钉死在选区左上角，外框/手柄占窗口外沿。
    const AnnotationEditorInPlacePlacement place =
        annotationEditorInPlacePlacement(screen_x, screen_y, source.width,
                                         source.height,
                                         data.controller.tool());
    x = place.window_x;
    y = place.window_y;
    data.image_origin_x = place.image_origin_x;
    data.image_origin_y = place.image_origin_y;
    data.client_width = place.window_width;
    window_width = place.window_width;
    window_height = place.window_height;
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
  SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
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
        invalidateImageArea(&data);
        continue;
      }
      if (data.selected_text_index != kInvalidAnnotationIndex)
      {
        clearTextSelection(&data);
        invalidateImageArea(&data);
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
        invalidateImageArea(&data);
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
