#include "qingying/annotate/annotation_overlay.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
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
const wchar_t kStrokePopupClassName[] = L"QingYingStrokePopup";
const wchar_t kInlineEditHostClassName[] = L"QingYingInlineEditHost";

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
constexpr UINT kButtonMoveId = 24;
constexpr UINT kInlineEditId = 10;
constexpr UINT kFontComboId = 20;
constexpr UINT kStrokePopupEditId = 21;
constexpr UINT kSizeMenuBaseId = 400;
constexpr UINT kTipShapeRectId = 200;
constexpr UINT kTipShapeEllipseId = 201;
constexpr UINT kTipFillId = 202;
constexpr UINT kTipLineStyleBaseId = 210;
constexpr UINT kTipStrokeId = 220;
constexpr UINT kTipColorBaseId = 230;
constexpr int kComboFontPx = 13;

constexpr int kInlineTextMaxChars = 256;
constexpr int kTextDragThresholdPx = 4;
constexpr int kTextHitPaddingPx = 10;
constexpr int kTextMinHitWidthPx = 28;
constexpr int kTextMinHitHeightPx = 20;
constexpr std::size_t kInvalidAnnotationIndex =
    static_cast<std::size_t>(-1);

constexpr int kToolbarIconItemCount = 9;
constexpr int kGeometryShapeCount = 2;
constexpr int kComboTooltipSlot = kToolbarIconItemCount;
constexpr int kShapeTooltipSlot = kComboTooltipSlot + 1;
constexpr int kFillTooltipSlot = kShapeTooltipSlot + kGeometryShapeCount;
constexpr int kLineStyleTooltipSlot = kFillTooltipSlot + 1;
constexpr int kStrokeTooltipSlot =
    kLineStyleTooltipSlot + AnnotationLineStyleCount;
constexpr int kColorTooltipSlot = kStrokeTooltipSlot + 1;
constexpr int kTooltipSlotCount =
    kColorTooltipSlot + AnnotationStylePresetColorCount;
constexpr COLORREF kStrokeSliderTrackColor = RGB(226, 229, 234);
constexpr COLORREF kStrokeSliderFillColor = RGB(64, 140, 255);
constexpr COLORREF kStrokeSliderThumbFill = RGB(255, 255, 255);
constexpr COLORREF kStrokePopupHintColor = RGB(140, 144, 150);
constexpr int kSwatchCornerRadius = 4;
constexpr COLORREF kSwatchBorderColor = RGB(160, 164, 170);
constexpr COLORREF kSwatchSelectedBorderColor = RGB(40, 44, 52);
constexpr COLORREF kFrameBorderColor = RGB(255, 128, 0);
constexpr COLORREF kHandleFillColor = RGB(255, 255, 255);
constexpr COLORREF kSizeLabelFillColor = RGB(60, 64, 70);
constexpr COLORREF kSizeLabelTextColor = RGB(255, 255, 255);
constexpr COLORREF kInlineEditBorderColor = RGB(255, 255, 255);
static_assert(static_cast<COLORREF>(AnnotationEditorInlineEditColorKeyRgb) ==
                  kToolbarColorKey,
              "inline edit color-key must match toolbar punch-through key");
constexpr COLORREF kTextChromeBorderColor = RGB(0, 0, 0);
constexpr COLORREF kTextDeleteFillColor = RGB(220, 56, 48);
constexpr COLORREF kTextDeleteGlyphColor = RGB(255, 255, 255);
constexpr int kTextDeleteGlyphWidthPx = 2;
constexpr int kTextDeleteGlyphInsetPx = 4;
constexpr int kSizeLabelFontPx = 12;
constexpr int kSizeLabelPadX = 6;
constexpr int kSizeLabelPadY = 2;
constexpr int kChromeInvalidateExtraPadPx = 2;
constexpr int kInlineCaretWidthPx = 1;
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
  RECT size_combo_rect{};
  HWND inline_edit{nullptr};
  HWND inline_edit_host{nullptr};
  WNDPROC inline_edit_prev_proc{nullptr};
  HFONT inline_edit_font{nullptr};
  HBRUSH inline_edit_key_brush{nullptr};
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
  RECT stroke_chip_rect{};
  RECT shape_rects[kGeometryShapeCount]{};
  RECT fill_rect{};
  RECT line_style_rects[AnnotationLineStyleCount]{};
  AnnotationTool last_geometry_tool{AnnotationTool::Rectangle};
  int toolbar_hover{-1};
  bool stroke_chip_hover{false};
  HWND stroke_popup{nullptr};
  HWND stroke_popup_edit{nullptr};
  bool stroke_syncing{false};
  bool stroke_slider_dragging{false};
  int toolbar_divider_x[AnnotationEditorDividerCount]{};
  HWND tooltip{nullptr};
  wchar_t tooltip_text[kTooltipSlotCount][kToolbarTooltipMaxChars]{};
  bool confirmed{false};
  int client_width{0};
  bool* loop_done{nullptr};
  int image_origin_x{AnnotationEditorFrameInsetPx};
  int image_origin_y{0};
  int image_screen_x{0};
  int image_screen_y{0};
  int chrome_offset_x{0};
  int chrome_offset_y{0};
  bool chrome_dragging{false};
  int chrome_drag_start_x{0};
  int chrome_drag_start_y{0};
  int chrome_drag_origin_x{0};
  int chrome_drag_origin_y{0};
  RECT main_bar_rect{};
  RECT property_bar_rect{};
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
void paintEditor(EditorWindowData* data, HDC hdc, bool draw_bar_shells = true,
                 bool draw_bar_items = true);
void paintEditorBuffered(EditorWindowData* data, HDC hdc);
void paintInlineEditFrame(HDC hdc, EditorWindowData* data);
void paintLiveInlineText(HDC hdc, EditorWindowData* data);
void placeInlineEditCaret(HWND edit, int caret, bool scroll_to_caret);
void layoutInlineEdit(EditorWindowData* data);
void positionOwnedPopup(HWND popup, HWND owner, int client_x, int client_y,
                        int width, int height);
void applyInlineEditVisual(EditorWindowData* data);
bool registerInlineEditHostClass(HINSTANCE instance);
LRESULT CALLBACK inlineEditHostWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam);
HBRUSH ensureInlineEditKeyBrush(EditorWindowData* data);
COLORREF colorBgraToRef(const ColorBgra& color);
void paintEditorToolbar(HDC hdc, EditorWindowData* data, bool draw_shell);
void canvasFromClient(const EditorWindowData* data, int x, int y, float& out_x,
                      float& out_y);
void paintPropertyBar(HDC hdc, EditorWindowData* data, bool draw_shell);
void layoutPropertyBar(HWND hwnd, EditorWindowData* data);
void layoutEditorChrome(HWND hwnd, EditorWindowData* data);
void bindEditorTooltips(EditorWindowData* data);
void resizeEditorChrome(EditorWindowData* data);
bool hitTestChromeBar(const EditorWindowData* data, int x, int y);
void beginChromeDrag(EditorWindowData* data, HWND hwnd, int x, int y);
void updateChromeDrag(EditorWindowData* data, int x, int y);
void endChromeDrag(EditorWindowData* data);
void syncGeometryButton(EditorWindowData* data);
int hitTestShapeToggle(const EditorWindowData* data, int x, int y);
bool hitTestFill(const EditorWindowData* data, int x, int y);
int hitTestLineStyle(const EditorWindowData* data, int x, int y);
void applyLiveTextStyle(EditorWindowData* data);
void syncStyleFromAnnotation(EditorWindowData* data,
                             const Annotation& annotation);
int hitTestColorSwatch(const EditorWindowData* data, int x, int y);
bool hitTestSizeCombo(const EditorWindowData* data, int x, int y);
void applyPickedSizeValue(EditorWindowData* data, bool mosaic, int value);
void pickSizeFromOverlayMenu(EditorWindowData* data);
bool handleSizeComboWheel(EditorWindowData* data, int delta);
bool hitTestStrokeChip(const EditorWindowData* data, int x, int y);
void hideStrokePopup(EditorWindowData* data);
void destroyStrokePopup(EditorWindowData* data);
void showStrokePopup(EditorWindowData* data);
void applyEditorStrokeWidth(EditorWindowData* data, int width);
bool handleStrokeChipWheel(EditorWindowData* data, int delta);
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

HBRUSH ensureInlineEditKeyBrush(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return nullptr;
  }
  if (data->inline_edit_key_brush == nullptr)
  {
    data->inline_edit_key_brush = CreateSolidBrush(kToolbarColorKey);
  }
  return data->inline_edit_key_brush;
}

void destroyInlineEdit(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }

  data->inline_edit_pressing = false;

  const HWND edit = data->inline_edit;
  const HWND host = data->inline_edit_host;
  // 必须先断开成员：DestroyWindow 会同步派发 EN_KILLFOCUS，
  // 否则 commitInlineText 会再次 add 同一条新文字。
  data->inline_edit = nullptr;
  data->inline_edit_host = nullptr;
  if (edit != nullptr && GetCapture() == edit)
  {
    ReleaseCapture();
  }
  if (edit != nullptr && data->inline_edit_prev_proc != nullptr)
  {
    SetWindowLongPtrW(edit, GWLP_WNDPROC,
                      reinterpret_cast<LONG_PTR>(data->inline_edit_prev_proc));
    data->inline_edit_prev_proc = nullptr;
  }
  if (host != nullptr)
  {
    DestroyWindow(host);
  }
  else if (edit != nullptr)
  {
    DestroyWindow(edit);
  }

  if (data->inline_edit_font != nullptr)
  {
    DeleteObject(data->inline_edit_font);
    data->inline_edit_font = nullptr;
  }
  if (data->inline_edit_key_brush != nullptr)
  {
    DeleteObject(data->inline_edit_key_brush);
    data->inline_edit_key_brush = nullptr;
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
    std::size_t index = edit_index;
    if (index == kInvalidAnnotationIndex)
    {
      const std::size_t count = data->session.engine().document().count();
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
  data->controller.setTool(AnnotationTool::Text);
  resizeEditorChrome(data);
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

  data->size_combo_syncing = true;
  for (int i = 0; i < AnnotationEditorFontSizeOptionCount; ++i)
  {
    if (AnnotationEditorFontSizeOptions[i] == font_size)
    {
      SendMessageW(data->font_combo, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
      data->size_combo_syncing = false;
      return;
    }
  }
  data->size_combo_syncing = false;
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
      DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace);
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
      (void)handleSizeComboWheel(
          data, static_cast<int>(static_cast<short>(HIWORD(wparam))));
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
        cancelInlineText(data);
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
  const HWND host = data->inline_edit_host != nullptr ? data->inline_edit_host
                                                      : data->inline_edit;
  positionOwnedPopup(host, data->overlay, data->image_origin_x + canvas_x,
                     data->image_origin_y + canvas_y, width, height);
  if (data->inline_edit_host != nullptr)
  {
    SetWindowPos(data->inline_edit, nullptr, 0, 0, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  placeInlineEditCaret(
      data->inline_edit, lstrlenW(buffer),
      annotationEditorInlineEditNeedsHScroll(text_extent, remain_width));
  invalidateInlineEditRegion(data);
  invalidateImageArea(data);
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

void paintLiveInlineText(HDC hdc, EditorWindowData* data)
{
  if (hdc == nullptr || data == nullptr || data->inline_edit == nullptr)
  {
    return;
  }

  wchar_t buffer[kInlineTextMaxChars]{};
  GetWindowTextW(data->inline_edit, buffer, kInlineTextMaxChars);
  const int text_len = lstrlenW(buffer);
  const int font_px =
      (std::min)((std::max)(data->controller.style().font_size, MinFontSize),
                 MaxFontSize);
  HFONT font = CreateFontW(
      -font_px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace);
  if (font == nullptr)
  {
    return;
  }

  const HGDIOBJ old_font = SelectObject(hdc, font);
  const COLORREF color = colorBgraToRef(data->controller.style().color);
  SetTextColor(hdc, color);
  SetBkMode(hdc, TRANSPARENT);

  const int origin_x =
      data->image_origin_x + static_cast<int>(data->text_anchor_x);
  const int origin_y =
      data->image_origin_y + static_cast<int>(data->text_anchor_y);
  if (text_len > 0)
  {
    RECT text_rect{origin_x, origin_y, origin_x + data->session.source().width,
                   origin_y + font_px + AnnotationEditorInlineEditHeightPad};
    DrawTextW(hdc, buffer, text_len, &text_rect,
              DT_LEFT | DT_TOP | DT_NOPREFIX | DT_SINGLELINE);
  }

  DWORD sel_start = 0;
  DWORD sel_end = 0;
  (void)SendMessageW(data->inline_edit, EM_GETSEL,
                     reinterpret_cast<WPARAM>(&sel_start),
                     reinterpret_cast<LPARAM>(&sel_end));
  int caret = static_cast<int>(sel_start);
  if (caret < 0)
  {
    caret = 0;
  }
  if (caret > text_len)
  {
    caret = text_len;
  }

  SIZE prefix{};
  if (caret > 0)
  {
    (void)GetTextExtentPoint32W(hdc, buffer, caret, &prefix);
  }
  const int caret_x = origin_x + static_cast<int>(prefix.cx);
  const HPEN pen = CreatePen(PS_SOLID, kInlineCaretWidthPx, color);
  if (pen != nullptr)
  {
    const HGDIOBJ old_pen = SelectObject(hdc, pen);
    MoveToEx(hdc, caret_x, origin_y, nullptr);
    LineTo(hdc, caret_x, origin_y + font_px);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
  }

  SelectObject(hdc, old_font);
  DeleteObject(font);
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

LRESULT CALLBACK inlineEditHostWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam)
{
  EditorWindowData* data = reinterpret_cast<EditorWindowData*>(
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
      if (data == nullptr || data->inline_edit == nullptr ||
          reinterpret_cast<HWND>(lparam) != data->inline_edit)
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
        (void)handleSizeComboWheel(
            data, static_cast<int>(static_cast<short>(HIWORD(wparam))));
      }
      return 0;
    case WM_COMMAND:
      if (data != nullptr && data->overlay != nullptr)
      {
        return SendMessageW(data->overlay, WM_COMMAND, wparam, lparam);
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

void beginInlineText(EditorWindowData* data, HWND hwnd, int x, int y,
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
    data->selected_text_index = edit_index;
  }

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
  const int remain_width = source.width - canvas_x;
  const int edit_width = annotationEditorInlineEditWidth(0, remain_width);
  const int edit_height =
      annotationEditorInlineEditHeight(data->controller.style().font_size);

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerInlineEditHostClass(instance) ||
      ensureInlineEditKeyBrush(data) == nullptr)
  {
    data->editing_text_index = kInvalidAnnotationIndex;
    return;
  }

  // 分层宿主打孔透出截图；子 EDIT 继续承接 IME。系统 EDIT 不能直接做 overlay 子窗。
  data->inline_edit_host = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kInlineEditHostClassName,
      L"", WS_POPUP | WS_CLIPCHILDREN, 0, 0, edit_width, edit_height, hwnd,
      nullptr, instance, data);
  if (data->inline_edit_host == nullptr)
  {
    data->editing_text_index = kInvalidAnnotationIndex;
    return;
  }
  applyToolbarColorKey(data->inline_edit_host);

  // 必须带 ES_AUTOHSCROLL，否则单行 EDIT 在旧宽度内会丢弃新字符，EN_CHANGE
  // 不会触发，输入框也就无法随文字变宽。布局后再清掉水平滚动残留。
  data->inline_edit = CreateWindowExW(
      0, L"EDIT", initial_text.c_str(),
      WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0, 0, edit_width,
      edit_height, data->inline_edit_host,
      reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kInlineEditId)),
      instance, nullptr);
  if (data->inline_edit == nullptr)
  {
    DestroyWindow(data->inline_edit_host);
    data->inline_edit_host = nullptr;
    data->editing_text_index = kInvalidAnnotationIndex;
    return;
  }
  SendMessageW(data->inline_edit, EM_SETLIMITTEXT, kInlineTextMaxChars - 1, 0);
  data->inline_edit_font = CreateFontW(
      -data->controller.style().font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
      FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace);
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
  ShowWindow(data->inline_edit_host, SW_SHOWNOACTIVATE);
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

  RECT local = data->size_combo_rect;
  const bool show_combo =
      annotationEditorPropertyBarPaintsSizeCombo(data->controller.tool()) &&
      local.right > local.left;

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

  bindToolbarTooltip(tooltip, overlay, kTipStrokeId, data->stroke_chip_rect,
                     toolbarIconLabel(ToolbarIconKind::StrokeWidth),
                     data->tooltip_text[kStrokeTooltipSlot],
                     kToolbarTooltipMaxChars);

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
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace);
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
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace);
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

void paintEditor(EditorWindowData* data, HDC hdc, bool draw_bar_shells,
                 bool draw_bar_items)
{
  if (data == nullptr || hdc == nullptr)
  {
    return;
  }

  RECT client{};
  GetClientRect(data->overlay, &client);
  fillToolbarColorKey(hdc, client);

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

void paintEditorBuffered(EditorWindowData* data, HDC hdc)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return;
  }
  (void)hdc;

  RECT client{};
  GetClientRect(data->overlay, &client);
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
    const HDC window_dc = GetDC(data->overlay);
    if (window_dc != nullptr)
    {
      paintEditor(data, window_dc, true);
      ReleaseDC(data->overlay, window_dc);
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
  (void)drawToolbarBarOnArgbBits(bits, width, height, data->main_bar_rect);
  if (data->property_bar_rect.right > data->property_bar_rect.left)
  {
    (void)drawToolbarBarOnArgbBits(bits, width, height, data->property_bar_rect);
  }
  paintEditorToolbar(mem_dc, data, false);
  paintPropertyBar(mem_dc, data, false);
  promoteRgbToOpaqueAlpha(bits, width, height);
  (void)presentLayeredArgbWindow(data->overlay, mem_dc, width, height);
  SelectObject(mem_dc, old_bitmap);
  DeleteDC(mem_dc);
  DeleteObject(dib);
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

  const std::size_t index = annotationEditorTextStyleTargetIndex(
      data->selected_text_index, data->editing_text_index,
      data->session.engine().document().count());
  if (index == AnnotationEditorInvalidIndex)
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

RECT toWinRect(const AnnotationEditorRect& rect)
{
  return RECT{rect.left, rect.top, rect.right, rect.bottom};
}

int currentStrokeWidthPx(const EditorWindowData* data)
{
  if (data == nullptr)
  {
    return static_cast<int>(DefaultStrokeWidth);
  }
  return annotationEditorStrokeWidthPx(data->controller.style().stroke_width);
}

void syncStrokePopupEdit(EditorWindowData* data)
{
  if (data == nullptr || data->stroke_popup_edit == nullptr ||
      data->stroke_syncing)
  {
    return;
  }

  wchar_t wanted[8]{};
  (void)swprintf_s(wanted, L"%d", currentStrokeWidthPx(data));
  wchar_t current[16]{};
  GetWindowTextW(data->stroke_popup_edit, current,
                 static_cast<int>(sizeof(current) / sizeof(current[0])));
  if (lstrcmpW(current, wanted) == 0)
  {
    return;
  }

  data->stroke_syncing = true;
  SetWindowTextW(data->stroke_popup_edit, wanted);
  data->stroke_syncing = false;
}

void applyEditorStrokeWidth(EditorWindowData* data, int width)
{
  if (data == nullptr)
  {
    return;
  }

  const int clamped = annotationEditorClampStrokeWidthPx(width);
  data->controller.setStrokeWidth(static_cast<float>(clamped));
  if (data->controller.isDrawing())
  {
    invalidateImageArea(data);
  }
  invalidateToolbar(data);
  if (data->stroke_popup != nullptr &&
      IsWindowVisible(data->stroke_popup) != FALSE)
  {
    InvalidateRect(data->stroke_popup, nullptr, FALSE);
  }
}

bool handleStrokeChipWheel(EditorWindowData* data, int delta)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsStroke(data->controller.tool()))
  {
    return false;
  }

  const int steps = annotationEditorWheelDeltaToSteps(delta);
  applyEditorStrokeWidth(
      data, annotationEditorStepStrokeWidth(currentStrokeWidthPx(data), steps));
  syncStrokePopupEdit(data);
  return true;
}

void hideStrokePopup(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->stroke_slider_dragging = false;
  if (data->stroke_popup != nullptr)
  {
    ShowWindow(data->stroke_popup, SW_HIDE);
  }
}

void destroyStrokePopup(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->stroke_slider_dragging = false;
  data->stroke_popup_edit = nullptr;
  if (data->stroke_popup != nullptr)
  {
    DestroyWindow(data->stroke_popup);
    data->stroke_popup = nullptr;
  }
}

void applyStrokeFromPopupSlider(EditorWindowData* data, int client_x)
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

void paintStrokePopup(HWND hwnd, EditorWindowData* data)
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

  HFONT font = data->combo_font;
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
  EditorWindowData* data = reinterpret_cast<EditorWindowData*>(
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
        data->stroke_slider_dragging = true;
        SetCapture(hwnd);
        applyStrokeFromPopupSlider(data, x);
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      if (data != nullptr && data->stroke_slider_dragging)
      {
        const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
        applyStrokeFromPopupSlider(data, x);
      }
      return 0;
    case WM_LBUTTONUP:
      if (data != nullptr)
      {
        data->stroke_slider_dragging = false;
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
      if (data == nullptr || data->stroke_syncing)
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
        GetWindowTextW(data->stroke_popup_edit, text,
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
        ScreenToClient(data->overlay, &pt);
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

void showStrokePopup(EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr ||
      !annotationEditorPropertyBarShowsStroke(data->controller.tool()))
  {
    return;
  }

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerStrokePopupClass(instance))
  {
    return;
  }

  RECT chip = data->stroke_chip_rect;
  POINT origin{chip.left, chip.bottom + AnnotationEditorButtonGap};
  ClientToScreen(data->overlay, &origin);

  if (data->stroke_popup == nullptr)
  {
    data->stroke_popup = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kStrokePopupClassName,
        L"", WS_POPUP | WS_CLIPCHILDREN, origin.x, origin.y,
        AnnotationEditorStrokePopupWidth, AnnotationEditorStrokePopupHeight,
        data->overlay, nullptr, instance, data);
    if (data->stroke_popup == nullptr)
    {
      return;
    }

    const AnnotationEditorStrokePopupLayout layout =
        annotationEditorStrokePopupLayout();
    const RECT value = toWinRect(layout.value);
    POINT edit_origin{value.left, value.top};
    ClientToScreen(data->stroke_popup, &edit_origin);
    data->stroke_popup_edit = CreateWindowExW(
        WS_EX_TOOLWINDOW, L"EDIT", L"",
        WS_POPUP | WS_VISIBLE | ES_NUMBER | ES_CENTER, edit_origin.x,
        edit_origin.y, value.right - value.left, value.bottom - value.top,
        data->stroke_popup, nullptr, instance, nullptr);
    if (data->stroke_popup_edit != nullptr)
    {
      SetWindowLongPtrW(data->stroke_popup_edit, GWLP_ID,
                        static_cast<LONG_PTR>(kStrokePopupEditId));
    }
    if (data->stroke_popup_edit != nullptr && data->combo_font != nullptr)
    {
      SendMessageW(data->stroke_popup_edit, WM_SETFONT,
                   reinterpret_cast<WPARAM>(data->combo_font), TRUE);
    }
  }
  else
  {
    SetWindowPos(data->stroke_popup, HWND_TOPMOST, origin.x, origin.y,
                 AnnotationEditorStrokePopupWidth,
                 AnnotationEditorStrokePopupHeight, SWP_NOACTIVATE);
  }

  syncStrokePopupEdit(data);
  ShowWindow(data->stroke_popup, SW_SHOW);
  SetForegroundWindow(data->stroke_popup);
  if (data->stroke_popup_edit != nullptr)
  {
    SetFocus(data->stroke_popup_edit);
    SendMessageW(data->stroke_popup_edit, EM_SETSEL, 0, -1);
  }
}

RECT takeToolbarButtonRect(int& x, int y)
{
  RECT rect{x, y, x + AnnotationEditorButtonWidth,
            y + AnnotationEditorButtonHeight};
  x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  return rect;
}

RECT takeToolbarSizedRect(int& x, int y, int width)
{
  RECT rect{x, y, x + width, y + AnnotationEditorButtonHeight};
  x += width + AnnotationEditorButtonGap;
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
  data->stroke_chip_rect = {};
  data->size_combo_rect = {};
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
  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    if (data->toolbar_items[static_cast<std::size_t>(i)].id != kButtonGeometryId)
    {
      continue;
    }
    data->toolbar_items[static_cast<std::size_t>(i)].icon =
        (data->last_geometry_tool == AnnotationTool::Ellipse)
            ? ToolbarIconKind::Ellipse
            : ToolbarIconKind::Rectangle;
    break;
  }
}

void layoutEditorChrome(HWND hwnd, EditorWindowData* data)
{
  if (hwnd == nullptr || data == nullptr)
  {
    return;
  }

  const Image& source = data->session.source();
  const int default_x = data->image_screen_x;
  const int default_y =
      data->image_screen_y + source.height + AnnotationEditorChromeImageGap;
  const int main_width = annotationEditorMainToolbarWidth();
  const int property_width =
      annotationEditorShowsPropertyBar(data->controller.tool())
          ? annotationEditorPropertyBarWidth()
          : 0;
  const int span_width =
      annotationEditorChromeSpanWidth(main_width, property_width);
  const int chrome_height =
      annotationEditorChromeHeight(data->controller.tool()) -
      AnnotationEditorChromeImageGap;
  const int screen_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int screen_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int screen_right = screen_left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int screen_bottom = screen_top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  annotationEditorClampChromeOffset(data->chrome_offset_x, data->chrome_offset_y,
                                    default_x, default_y, span_width,
                                    chrome_height, screen_left, screen_top,
                                    screen_right, screen_bottom);

  const int chrome_screen_x = default_x + data->chrome_offset_x;
  const int chrome_screen_y = default_y + data->chrome_offset_y;
  // 窗口已铺满虚拟屏，禁止再 SetWindowPos：拖栏时改窗口原点会让截图框先跟着走再被重绘拉回，边缘处明显抖动。
  const int bar_left = chrome_screen_x - screen_left;
  const int bar_top = chrome_screen_y - screen_top;
  const int bar_height = annotationEditorToolbarHeight();
  data->main_bar_rect = {bar_left, bar_top, bar_left + main_width,
                         bar_top + bar_height};

  const int y = bar_top + AnnotationEditorBarPadding;
  int x = bar_left + AnnotationEditorBarPadding;

  const struct
  {
    UINT id;
    ToolbarIconKind icon;
    bool accent;
  } left_items[] = {
      {kButtonMoveId, ToolbarIconKind::Move, false},
      {kButtonGeometryId, ToolbarIconKind::Rectangle, false},
      {kButtonArrowId, ToolbarIconKind::Arrow, false},
      {kButtonPenId, ToolbarIconKind::Pen, false},
      {kButtonMosaicId, ToolbarIconKind::Mosaic, false},
      {kButtonTextId, ToolbarIconKind::Text, false},
  };

  int item_index = 0;
  for (const auto& spec : left_items)
  {
    data->toolbar_items[static_cast<std::size_t>(item_index)].id = spec.id;
    data->toolbar_items[static_cast<std::size_t>(item_index)].icon = spec.icon;
    data->toolbar_items[static_cast<std::size_t>(item_index)].accent = spec.accent;
    data->toolbar_items[static_cast<std::size_t>(item_index)].rect = {
        x, y, x + AnnotationEditorButtonWidth, y + AnnotationEditorButtonHeight};
    x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
    ++item_index;
  }

  data->toolbar_divider_x[0] =
      x - AnnotationEditorButtonGap + AnnotationEditorDividerGap / 2;
  x += AnnotationEditorDividerGap - AnnotationEditorButtonGap;

  data->toolbar_items[static_cast<std::size_t>(item_index)] = {
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
      (std::max)(x, bar_left + main_width - action_total -
                        AnnotationEditorBarPadding);

  data->toolbar_items[static_cast<std::size_t>(item_index)] = {
      kButtonConfirmId,
      ToolbarIconKind::Confirm,
      true,
      {confirm_x, y, confirm_x + AnnotationEditorButtonWidth,
       y + AnnotationEditorButtonHeight}};
  ++item_index;

  const int cancel_x =
      confirm_x + AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  data->toolbar_items[static_cast<std::size_t>(item_index)] = {
      kButtonCancelId,
      ToolbarIconKind::Cancel,
      false,
      {cancel_x, y, cancel_x + AnnotationEditorButtonWidth,
       y + AnnotationEditorButtonHeight}};

  layoutPropertyBar(hwnd, data);
  syncGeometryButton(data);
  bindEditorTooltips(data);
  if (data->inline_edit != nullptr)
  {
    layoutInlineEdit(data);
  }
}

void layoutPropertyBar(HWND hwnd, EditorWindowData* data)
{
  if (hwnd == nullptr || data == nullptr)
  {
    return;
  }

  data->property_bar_rect = {};
  const AnnotationTool tool = data->controller.tool();
  resetPropertyBarRects(data);
  if (!annotationEditorShowsPropertyBar(tool))
  {
    if (data->font_combo != nullptr)
    {
      ShowWindow(data->font_combo, SW_HIDE);
    }
    return;
  }

  const int y = data->main_bar_rect.bottom + AnnotationEditorChromeStackGap +
                AnnotationEditorBarPadding;
  const int bar_left = data->main_bar_rect.left;
  const int bar_top = data->main_bar_rect.bottom + AnnotationEditorChromeStackGap;
  const int swatch = AnnotationEditorColorSwatchSize;
  const int swatch_y = y + (AnnotationEditorButtonHeight - swatch) / 2;
  int x = bar_left + AnnotationEditorBarPadding;
  const int mosaic_combo_x = x;

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
    data->stroke_chip_rect =
        takeToolbarSizedRect(x, y, AnnotationEditorStrokeChipWidth);
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
    data->stroke_chip_rect =
        takeToolbarSizedRect(x, y, AnnotationEditorStrokeChipWidth);
  }

  data->size_combo_rect = {};
  if (annotationEditorPropertyBarShowsSizeCombo(tool))
  {
    const int placed_x =
        annotationEditorPropertyBarShowsMosaicSize(tool) ? mosaic_combo_x
                                                         : combo_x;
    data->size_combo_rect = {placed_x, y,
                             placed_x + AnnotationEditorFontComboWidth,
                             y + AnnotationEditorButtonHeight};
    x = (std::max)(x, placed_x + AnnotationEditorFontComboWidth +
                          AnnotationEditorButtonGap);
  }
  if (data->font_combo != nullptr)
  {
    ShowWindow(data->font_combo, SW_HIDE);
  }

  const int content_right =
      (std::max)(x - AnnotationEditorButtonGap,
                 bar_left + AnnotationEditorBarPadding);
  data->property_bar_rect = {
      bar_left, bar_top,
      content_right + AnnotationEditorBarPadding,
      bar_top + annotationEditorToolbarHeight()};

  bindPropertyBarTooltips(data);
}

void resizeEditorChrome(EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr)
  {
    return;
  }

  const AnnotationTool tool = data->controller.tool();
  fillSizeCombo(data);
  if (!annotationEditorPropertyBarShowsStroke(tool))
  {
    hideStrokePopup(data);
  }
  if (data->font_combo != nullptr)
  {
    // 全屏 UpdateLayeredWindow 会盖住 WS_POPUP ComboBox；字号改由 overlay 绘制/命中。
    ShowWindow(data->font_combo, SW_HIDE);
  }
  layoutEditorChrome(data->overlay, data);
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

bool hitTestSizeCombo(const EditorWindowData* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarPaintsSizeCombo(data->controller.tool()))
  {
    return false;
  }

  const POINT pt{x, y};
  return PtInRect(&data->size_combo_rect, pt) != FALSE;
}

void refreshInlineEditFont(EditorWindowData* data)
{
  if (data == nullptr || data->inline_edit == nullptr)
  {
    return;
  }
  if (data->inline_edit_font != nullptr)
  {
    DeleteObject(data->inline_edit_font);
    data->inline_edit_font = nullptr;
  }
  data->inline_edit_font = CreateFontW(
      -data->controller.style().font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
      FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace);
  if (data->inline_edit_font != nullptr)
  {
    SendMessageW(data->inline_edit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(data->inline_edit_font), TRUE);
  }
  applyInlineEditVisual(data);
  SetFocus(data->inline_edit);
}

void applyPickedSizeValue(EditorWindowData* data, bool mosaic, int value)
{
  if (data == nullptr)
  {
    return;
  }
  if (mosaic)
  {
    data->controller.setMosaicBlockSize(value);
    if (data->controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    invalidateToolbar(data);
    return;
  }

  selectFontSizeInCombo(data, value);
  refreshInlineEditFont(data);
  applyLiveTextStyle(data);
  invalidateToolbar(data);
}

bool handleSizeComboWheel(EditorWindowData* data, int delta)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarPaintsSizeCombo(data->controller.tool()))
  {
    return false;
  }

  const bool mosaic =
      annotationEditorPropertyBarShowsMosaicSize(data->controller.tool());
  const int current = mosaic ? data->controller.mosaicBlockSize()
                             : data->controller.style().font_size;
  const int steps = annotationEditorWheelDeltaToSteps(delta);
  const int next = mosaic ? annotationEditorStepMosaicBlockSize(current, steps)
                          : annotationEditorStepFontSize(current, steps);
  applyPickedSizeValue(data, mosaic, next);
  return true;
}

void pickSizeFromOverlayMenu(EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr)
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
  const int current_index =
      annotationEditorLookupSizeOptionIndex(options, count, current);
  const int next_index =
      (current_index < 0) ? 0 : ((current_index + 1) % count);
  const int value =
      annotationEditorSizeOptionAt(options, count, next_index, current);

  const HMENU menu = CreatePopupMenu();
  if (menu == nullptr)
  {
    applyPickedSizeValue(data, mosaic, value);
    return;
  }

  for (int i = 0; i < count; ++i)
  {
    wchar_t label[16]{};
    (void)swprintf_s(label, L"%d", options[i]);
    UINT flags = MF_STRING;
    if (options[i] == current)
    {
      flags |= MF_CHECKED;
    }
    if (AppendMenuW(menu, flags, kSizeMenuBaseId + static_cast<UINT>(i),
                    label) == FALSE)
    {
      DestroyMenu(menu);
      applyPickedSizeValue(data, mosaic, value);
      return;
    }
  }

  POINT origin{data->size_combo_rect.left, data->size_combo_rect.bottom};
  ClientToScreen(data->overlay, &origin);
  (void)SetForegroundWindow(data->overlay);
  const UINT cmd = TrackPopupMenu(
      menu,
      TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY |
          TPM_RETURNCMD,
      origin.x, origin.y, 0, data->overlay, nullptr);
  DestroyMenu(menu);

  const int index =
      annotationEditorSizeMenuCommandToIndex(cmd, kSizeMenuBaseId, count);
  if (index < 0)
  {
    return;
  }
  const int picked =
      annotationEditorSizeOptionAt(options, count, index, current);
  applyPickedSizeValue(data, mosaic, picked);
}

bool hitTestStrokeChip(const EditorWindowData* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsStroke(data->controller.tool()))
  {
    return false;
  }

  const POINT pt{x, y};
  return PtInRect(&data->stroke_chip_rect, pt) != FALSE;
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

  if (hitTestSizeCombo(data, x, y))
  {
    pickSizeFromOverlayMenu(data);
    return;
  }

  if (hitTestStrokeChip(data, x, y))
  {
    if (data->stroke_popup != nullptr &&
        IsWindowVisible(data->stroke_popup) != FALSE)
    {
      hideStrokePopup(data);
    }
    else
    {
      showStrokePopup(data);
    }
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
         hitTestChromeBar(data, pt.x, pt.y) ||
         hitTestColorSwatch(data, pt.x, pt.y) >= 0 ||
         hitTestSizeCombo(data, pt.x, pt.y) ||
         hitTestStrokeChip(data, pt.x, pt.y) ||
         hitTestShapeToggle(data, pt.x, pt.y) >= 0 ||
         hitTestFill(data, pt.x, pt.y) ||
         hitTestLineStyle(data, pt.x, pt.y) >= 0;
}

void paintPropertyBar(HDC hdc, EditorWindowData* data, bool draw_shell)
{
  if (hdc == nullptr || data == nullptr ||
      !annotationEditorShowsPropertyBar(data->controller.tool()))
  {
    return;
  }

  if (data->property_bar_rect.right <= data->property_bar_rect.left)
  {
    return;
  }

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  if (draw_shell)
  {
    drawToolbarBar(hdc, data->property_bar_rect);
  }

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

  if (annotationEditorPropertyBarPaintsSizeCombo(tool) &&
      data->size_combo_rect.right > data->size_combo_rect.left)
  {
    const RECT& chip = data->size_combo_rect;
    fillRoundRect(hdc, chip, colors.selected_fill, kSwatchBorderColor,
                  DefaultModernToolbarMetrics.hover_radius);
    const bool mosaic =
        annotationEditorPropertyBarShowsMosaicSize(tool);
    const int value = mosaic ? data->controller.mosaicBlockSize()
                             : style.font_size;
    wchar_t value_text[8]{};
    (void)swprintf_s(value_text, L"%d", value);
    RECT text_rect = chip;
    const HGDIOBJ old_font =
        (data->combo_font != nullptr) ? SelectObject(hdc, data->combo_font)
                                      : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, colors.label);
    DrawTextW(hdc, value_text, -1, &text_rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (old_font != nullptr)
    {
      SelectObject(hdc, old_font);
    }
  }

  if (!annotationEditorPropertyBarShowsStroke(tool))
  {
    return;
  }

  const int divider_x =
      data->stroke_chip_rect.left - AnnotationEditorDividerGap / 2;
  drawToolbarDivider(hdc, divider_x, data->property_bar_rect.top + 8,
                     data->property_bar_rect.bottom - 8);

  const RECT& chip = data->stroke_chip_rect;
  const bool popup_open = data->stroke_popup != nullptr &&
                          IsWindowVisible(data->stroke_popup) != FALSE;
  if (data->stroke_chip_hover || popup_open)
  {
    fillRoundRect(hdc, chip,
                  data->stroke_chip_hover ? colors.hover_fill
                                          : colors.selected_fill,
                  data->stroke_chip_hover ? colors.hover_fill
                                          : colors.selected_fill,
                  DefaultModernToolbarMetrics.hover_radius);
  }

  RECT icon{chip.left, chip.top,
            chip.left + AnnotationEditorStrokeChipIconWidth, chip.bottom};
  drawToolbarIcon(hdc, icon, ToolbarIconKind::StrokeWidth, colors.icon);

  wchar_t value_text[8]{};
  (void)swprintf_s(value_text, L"%d",
                   annotationEditorStrokeWidthPx(style.stroke_width));
  RECT value{icon.right, chip.top, chip.right, chip.bottom};
  const HGDIOBJ old_font =
      (data->combo_font != nullptr) ? SelectObject(hdc, data->combo_font)
                                    : nullptr;
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, colors.label);
  DrawTextW(hdc, value_text, -1, &value,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  if (old_font != nullptr)
  {
    SelectObject(hdc, old_font);
  }
}

void paintEditorToolbar(HDC hdc, EditorWindowData* data, bool draw_shell)
{
  if (hdc == nullptr || data == nullptr)
  {
    return;
  }

  if (draw_shell)
  {
    drawToolbarBar(hdc, data->main_bar_rect);
  }

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

  const int divider_top = data->main_bar_rect.top + 8;
  const int divider_bottom = data->main_bar_rect.bottom - 8;
  for (int i = 0; i < AnnotationEditorDividerCount; ++i)
  {
    if (data->toolbar_divider_x[i] > 0)
    {
      drawToolbarDivider(hdc, data->toolbar_divider_x[i], divider_top,
                         divider_bottom);
    }
  }
}

void positionOwnedPopup(HWND popup, HWND owner, int client_x, int client_y,
                        int width, int height)
{
  if (popup == nullptr || owner == nullptr)
  {
    return;
  }
  POINT origin{client_x, client_y};
  ClientToScreen(owner, &origin);
  SetWindowPos(popup, HWND_TOPMOST, origin.x, origin.y, width, height,
               SWP_NOACTIVATE);
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

  data->font_combo = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"COMBOBOX", L"",
      WS_POPUP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0,
      AnnotationEditorFontComboWidth, AnnotationEditorFontComboDropHeight, hwnd,
      nullptr, GetModuleHandleW(nullptr), nullptr);
  if (data->font_combo == nullptr)
  {
    return false;
  }
  SetWindowLongPtrW(data->font_combo, GWLP_ID,
                    static_cast<LONG_PTR>(kFontComboId));

  data->combo_font = CreateFontW(
      -kComboFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace);
  if (data->combo_font != nullptr)
  {
    SendMessageW(data->font_combo, WM_SETFONT,
                 reinterpret_cast<WPARAM>(data->combo_font), TRUE);
  }

  data->controller.setFontSize(DefaultFontSize);
  data->controller.setMosaicBlockSize(DefaultMosaicBlockSize);
  fillSizeCombo(data);

  resizeEditorChrome(data);

  data->tooltip = createToolbarTooltip(hwnd);
  bindEditorTooltips(data);
  return true;
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
  InvalidateRect(data->overlay, nullptr, FALSE);
}

bool hitTestChromeBar(const EditorWindowData* data, int x, int y)
{
  if (data == nullptr)
  {
    return false;
  }
  const POINT pt{x, y};
  if (PtInRect(&data->main_bar_rect, pt) != FALSE)
  {
    return true;
  }
  return PtInRect(&data->property_bar_rect, pt) != FALSE;
}

void bindEditorTooltips(EditorWindowData* data)
{
  if (data == nullptr || data->overlay == nullptr || data->tooltip == nullptr)
  {
    return;
  }
  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    const wchar_t* tip =
        (data->toolbar_items[static_cast<std::size_t>(i)].id ==
         kButtonGeometryId)
            ? toolbarIconLabel(ToolbarIconKind::Geometry)
            : toolbarIconLabel(
                  data->toolbar_items[static_cast<std::size_t>(i)].icon);
    bindToolbarTooltip(data->tooltip, data->overlay,
                       data->toolbar_items[static_cast<std::size_t>(i)].id,
                       data->toolbar_items[static_cast<std::size_t>(i)].rect,
                       tip, data->tooltip_text[static_cast<std::size_t>(i)],
                       kToolbarTooltipMaxChars);
  }
  bindPropertyBarTooltips(data);
}

RECT unionChromeRects(const RECT& main_bar, const RECT& property_bar)
{
  if (IsRectEmpty(&property_bar) != FALSE)
  {
    return main_bar;
  }
  if (IsRectEmpty(&main_bar) != FALSE)
  {
    return property_bar;
  }
  RECT combined{};
  (void)UnionRect(&combined, &main_bar, &property_bar);
  return combined;
}

void invalidateChromeMove(HWND hwnd, const RECT& old_rect, const RECT& new_rect)
{
  if (hwnd == nullptr)
  {
    return;
  }
  RECT dirty{};
  if (IsRectEmpty(&old_rect) != FALSE)
  {
    dirty = new_rect;
  }
  else if (IsRectEmpty(&new_rect) != FALSE)
  {
    dirty = old_rect;
  }
  else
  {
    (void)UnionRect(&dirty, &old_rect, &new_rect);
  }
  const int pad = DefaultModernToolbarMetrics.corner_radius +
                  kChromeInvalidateExtraPadPx;
  dirty.left -= pad;
  dirty.top -= pad;
  dirty.right += pad;
  dirty.bottom += pad;
  InvalidateRect(hwnd, &dirty, FALSE);
}

void beginChromeDrag(EditorWindowData* data, HWND hwnd, int /*x*/, int /*y*/)
{
  if (data == nullptr || hwnd == nullptr)
  {
    return;
  }
  POINT cursor{};
  if (GetCursorPos(&cursor) == FALSE)
  {
    return;
  }
  data->chrome_dragging = true;
  data->chrome_drag_start_x = cursor.x;
  data->chrome_drag_start_y = cursor.y;
  data->chrome_drag_origin_x = data->chrome_offset_x;
  data->chrome_drag_origin_y = data->chrome_offset_y;
  hideStrokePopup(data);
  SetCapture(hwnd);
}

void updateChromeDrag(EditorWindowData* data, int /*x*/, int /*y*/)
{
  if (data == nullptr || !data->chrome_dragging || data->overlay == nullptr)
  {
    return;
  }
  POINT cursor{};
  if (GetCursorPos(&cursor) == FALSE)
  {
    return;
  }
  data->chrome_offset_x =
      data->chrome_drag_origin_x + (cursor.x - data->chrome_drag_start_x);
  data->chrome_offset_y =
      data->chrome_drag_origin_y + (cursor.y - data->chrome_drag_start_y);
  const RECT old_chrome =
      unionChromeRects(data->main_bar_rect, data->property_bar_rect);
  layoutEditorChrome(data->overlay, data);
  const RECT new_chrome =
      unionChromeRects(data->main_bar_rect, data->property_bar_rect);
  invalidateChromeMove(data->overlay, old_chrome, new_chrome);
}

void endChromeDrag(EditorWindowData* data)
{
  if (data == nullptr)
  {
    return;
  }
  if (data->chrome_dragging)
  {
    data->chrome_dragging = false;
    ReleaseCapture();
  }
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
  if (data == nullptr || id == kButtonMoveId)
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
    case WM_SETCURSOR:
    {
      if (data != nullptr && data->toolbar_hover >= 0 &&
          data->toolbar_items[static_cast<std::size_t>(data->toolbar_hover)]
                  .id == kButtonMoveId)
      {
        SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32646)));  // IDC_SIZEALL
        return TRUE;
      }
      break;
    }
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
          data->toolbar_items[static_cast<std::size_t>(hit)].id ==
              kButtonMoveId)
      {
        beginChromeDrag(data, hwnd, x, y);
        return 0;
      }
      if (hit >= 0)
      {
        handleToolbarItemClick(data,
                               data->toolbar_items[static_cast<std::size_t>(hit)]
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
        data->controller.setTool(AnnotationTool::Text);
        resizeEditorChrome(data);
        beginOrEditTextAt(data, hwnd, x, y);
        return 0;
      }
      if (!pointInImageArea(data, x, y))
      {
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
      if (data->controller.tool() != AnnotationTool::Text &&
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
        data->controller.setTool(AnnotationTool::Text);
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
      if (data->chrome_dragging)
      {
        updateChromeDrag(data, x, y);
        return 0;
      }
      if (hitTestChromeBar(data, x, y) &&
          !data->controller.isDrawing() && !data->text_gesture_active)
      {
        const int hit = hitTestEditorToolbar(data, x, y);
        const bool chip_hover = hitTestStrokeChip(data, x, y);
        if (hit != data->toolbar_hover ||
            chip_hover != data->stroke_chip_hover)
        {
          data->toolbar_hover = hit;
          data->stroke_chip_hover = chip_hover;
          invalidateToolbar(data);
        }
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        TrackMouseEvent(&track);
        return 0;
      }
      if (data->toolbar_hover >= 0 || data->stroke_chip_hover)
      {
        data->toolbar_hover = -1;
        data->stroke_chip_hover = false;
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
      if (data != nullptr &&
          (data->toolbar_hover >= 0 || data->stroke_chip_hover))
      {
        data->toolbar_hover = -1;
        data->stroke_chip_hover = false;
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
          (data->stroke_popup != nullptr &&
           IsWindowVisible(data->stroke_popup) != FALSE))
      {
        (void)handleStrokeChipWheel(data, delta);
        return 0;
      }
      if (annotationEditorWheelAdjustsSize(
              data->controller.tool(), hitTestSizeCombo(data, pt.x, pt.y)))
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
      if (data->chrome_dragging)
      {
        endChromeDrag(data);
        return 0;
      }
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
      const HWND combo = reinterpret_cast<HWND>(lparam);
      if ((id == kFontComboId || combo == data->font_combo) &&
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
        destroyStrokePopup(data);
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
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
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
  data.image_screen_x = data.image_origin_x;
  data.image_screen_y = data.image_origin_y;
  data.controller.setCanvasSize(source.width, source.height);
  data.controller.setTool(AnnotationTool::None);
  data.callback = std::move(callback);

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerEditorClass(instance))
  {
    return false;
  }

  const int desk_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int desk_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int desk_width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int desk_height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (screen_x < 0 || screen_y < 0)
  {
    data.image_screen_x =
        (std::max)(0, (GetSystemMetrics(SM_CXSCREEN) - source.width) / 2);
    data.image_screen_y =
        (std::max)(0, (GetSystemMetrics(SM_CYSCREEN) - source.height) / 2);
  }
  else
  {
    data.image_screen_x = screen_x;
    data.image_screen_y = screen_y;
  }
  const AnnotationEditorVirtualDesktopPlacement place =
      annotationEditorVirtualDesktopPlacement(data.image_screen_x,
                                              data.image_screen_y, desk_left,
                                              desk_top, desk_width, desk_height);
  const int x = place.window_x;
  const int y = place.window_y;
  const int window_width = place.window_width;
  const int window_height = place.window_height;
  data.image_origin_x = place.image_origin_x;
  data.image_origin_y = place.image_origin_y;
  data.client_width = place.window_width;

  bool done = false;
  data.loop_done = &done;

  const DWORD style = WS_POPUP | WS_VISIBLE;
  const HWND hwnd = CreateWindowExW(
      WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kOverlayClassName, L"", style, x, y,
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
      if (data.stroke_popup != nullptr &&
          IsWindowVisible(data.stroke_popup) != FALSE)
      {
        hideStrokePopup(&data);
        continue;
      }
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
