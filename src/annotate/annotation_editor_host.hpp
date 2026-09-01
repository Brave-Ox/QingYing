#pragma once

#include <Windows.h>

#include <cstddef>

#include "annotate/gdi_raii.hpp"
#include "qingying/annotate/annotation_editor_layout.hpp"
#include "qingying/annotate/annotation_editor_session.hpp"
#include "qingying/annotate/annotation_interaction_controller.hpp"
#include "qingying/annotate/annotation_renderer.hpp"
#include "qingying/annotate/annotation_result.hpp"
#include "qingying/annotate/annotation_types.hpp"
#include "qingying/ui/modern_toolbar.hpp"

namespace qingying {

inline constexpr wchar_t kOverlayClassName[] = L"QingYingAnnotationOverlay";
inline constexpr wchar_t kStrokePopupClassName[] = L"QingYingStrokePopup";
inline constexpr wchar_t kInlineEditHostClassName[] = L"QingYingInlineEditHost";
inline constexpr wchar_t kEditorHwndPropName[] = L"QingYingAnnotationHwnd";

inline constexpr UINT kButtonConfirmId = 1;
inline constexpr UINT kButtonCancelId = 2;
inline constexpr UINT kButtonRectId = 3;
inline constexpr UINT kButtonEllipseId = 4;
inline constexpr UINT kButtonArrowId = 5;
inline constexpr UINT kButtonPenId = 6;
inline constexpr UINT kButtonTextId = 7;
inline constexpr UINT kButtonUndoId = 8;
inline constexpr UINT kButtonMosaicId = 9;
inline constexpr UINT kButtonGeometryId = 23;
inline constexpr UINT kButtonMoveId = 24;
inline constexpr UINT kInlineEditId = 10;
inline constexpr UINT kFontComboId = 20;
inline constexpr UINT kStrokePopupEditId = 21;
inline constexpr UINT_PTR kStrokePopupEditSubclassId = 1;
inline constexpr UINT kSizeMenuBaseId = 400;
inline constexpr UINT kTipShapeRectId = 200;
inline constexpr UINT kTipShapeEllipseId = 201;
inline constexpr UINT kTipFillId = 202;
inline constexpr UINT kTipLineStyleBaseId = 210;
inline constexpr UINT kTipStrokeId = 220;
inline constexpr UINT kTipColorBaseId = 230;
inline constexpr int kComboFontPx = 13;

inline constexpr int kInlineTextMaxChars = 256;
inline constexpr int kTextDragThresholdPx = 4;
inline constexpr int kTextHitPaddingPx = 10;
inline constexpr int kTextMinHitWidthPx = 28;
inline constexpr int kTextMinHitHeightPx = 20;
inline constexpr std::size_t kInvalidAnnotationIndex =
    static_cast<std::size_t>(-1);

inline constexpr int kToolbarIconItemCount = 9;
inline constexpr int kGeometryShapeCount = 2;
inline constexpr int kComboTooltipSlot = kToolbarIconItemCount;
inline constexpr int kShapeTooltipSlot = kComboTooltipSlot + 1;
inline constexpr int kFillTooltipSlot = kShapeTooltipSlot + kGeometryShapeCount;
inline constexpr int kLineStyleTooltipSlot = kFillTooltipSlot + 1;
inline constexpr int kStrokeTooltipSlot =
    kLineStyleTooltipSlot + AnnotationLineStyleCount;
inline constexpr int kColorTooltipSlot = kStrokeTooltipSlot + 1;
inline constexpr int kTooltipSlotCount =
    kColorTooltipSlot + AnnotationStylePresetColorCount;
inline constexpr COLORREF kStrokeSliderTrackColor = RGB(226, 229, 234);
inline constexpr COLORREF kStrokeSliderFillColor = RGB(64, 140, 255);
inline constexpr COLORREF kStrokeSliderThumbFill = RGB(255, 255, 255);
inline constexpr COLORREF kStrokePopupHintColor = RGB(140, 144, 150);
inline constexpr int kSwatchCornerRadius = 4;
inline constexpr COLORREF kSwatchBorderColor = RGB(160, 164, 170);
inline constexpr COLORREF kSwatchSelectedBorderColor = RGB(40, 44, 52);
inline constexpr COLORREF kFrameBorderColor = RGB(255, 128, 0);
inline constexpr COLORREF kHandleFillColor = RGB(255, 255, 255);
inline constexpr COLORREF kSizeLabelFillColor = RGB(60, 64, 70);
inline constexpr COLORREF kSizeLabelTextColor = RGB(255, 255, 255);
inline constexpr COLORREF kInlineEditBorderColor = RGB(255, 255, 255);
static_assert(static_cast<COLORREF>(AnnotationEditorInlineEditColorKeyRgb) ==
                  kToolbarColorKey,
              "inline edit color-key must match toolbar punch-through key");
inline constexpr COLORREF kTextChromeBorderColor = RGB(0, 0, 0);
inline constexpr COLORREF kTextDeleteFillColor = RGB(220, 56, 48);
inline constexpr COLORREF kTextDeleteGlyphColor = RGB(255, 255, 255);
inline constexpr int kTextDeleteGlyphWidthPx = 2;
inline constexpr int kTextDeleteGlyphInsetPx = 4;
inline constexpr int kSizeLabelFontPx = 12;
inline constexpr int kSizeLabelPadX = 6;
inline constexpr int kSizeLabelPadY = 2;
inline constexpr int kChromeInvalidateExtraPadPx = 2;
inline constexpr int kInlineCaretWidthPx = 1;
inline constexpr UINT kMsgCancelFromBackdrop = WM_APP + 2;

struct EditorToolbarItem
{
  UINT id{0};
  ToolbarIconKind icon{ToolbarIconKind::Rectangle};
  bool accent{false};
  RECT rect{};
};

// 标注编辑器窗口状态。公开字段仅供 annotate 内部编译单元互调；
// 第三人不得包含本头。所有权由 AnnotationOverlay 的 unique_ptr 持有。
class AnnotationEditorHost
{
 public:
  AnnotationEditorHost() = default;
  ~AnnotationEditorHost() = default;

  AnnotationEditorHost(const AnnotationEditorHost&) = delete;
  AnnotationEditorHost& operator=(const AnnotationEditorHost&) = delete;

  AnnotationEditorSession m_session;
  AnnotationInteractionController m_controller;
  AnnotationRenderer m_renderer;
  AnnotationCallback m_callback;
  HWND m_overlay{nullptr};
  HWND m_font_combo{nullptr};
  GdiObject m_combo_font;
  bool m_size_combo_syncing{false};
  RECT m_size_combo_rect{};
  HWND m_inline_edit{nullptr};
  HWND m_inline_edit_host{nullptr};
  WNDPROC m_inline_edit_prev_proc{nullptr};
  GdiObject m_inline_edit_font;
  GdiObject m_inline_edit_key_brush;
  float m_text_anchor_x{0.0f};
  float m_text_anchor_y{0.0f};
  std::size_t m_editing_text_index{kInvalidAnnotationIndex};
  std::size_t m_selected_text_index{kInvalidAnnotationIndex};
  DWORD m_last_text_click_tick{0};
  std::size_t m_last_text_click_index{kInvalidAnnotationIndex};
  int m_last_text_click_x{0};
  int m_last_text_click_y{0};
  bool m_inline_edit_pressing{false};
  int m_inline_edit_press_x{0};
  int m_inline_edit_press_y{0};
  bool m_inline_commit_busy{false};
  bool m_text_gesture_active{false};
  bool m_text_dragging{false};
  std::size_t m_text_target_index{kInvalidAnnotationIndex};
  float m_text_press_x{0.0f};
  float m_text_press_y{0.0f};
  float m_text_origin_x{0.0f};
  float m_text_origin_y{0.0f};
  float m_text_drag_x{0.0f};
  float m_text_drag_y{0.0f};
  EditorToolbarItem m_toolbar_items[kToolbarIconItemCount]{};
  RECT m_color_swatch_rects[AnnotationStylePresetColorCount]{};
  RECT m_stroke_chip_rect{};
  RECT m_shape_rects[kGeometryShapeCount]{};
  RECT m_fill_rect{};
  RECT m_line_style_rects[AnnotationLineStyleCount]{};
  AnnotationTool m_last_geometry_tool{AnnotationTool::Rectangle};
  int m_toolbar_hover{-1};
  bool m_stroke_chip_hover{false};
  HWND m_stroke_popup{nullptr};
  HWND m_stroke_popup_edit{nullptr};
  bool m_stroke_syncing{false};
  bool m_stroke_slider_dragging{false};
  int m_toolbar_divider_x[AnnotationEditorDividerCount]{};
  HWND m_tooltip{nullptr};
  wchar_t m_tooltip_text[kTooltipSlotCount][kToolbarTooltipMaxChars]{};
  bool m_confirmed{false};
  int m_client_width{0};
  HWND* m_owner_hwnd{nullptr};
  bool* m_owner_visible{nullptr};
  bool* m_owner_suppress_callback{nullptr};
  bool* m_destroyed_during_create{nullptr};
  int m_image_origin_x{AnnotationEditorFrameInsetPx};
  int m_image_origin_y{0};
  int m_image_screen_x{0};
  int m_image_screen_y{0};
  int m_chrome_offset_x{0};
  int m_chrome_offset_y{0};
  bool m_chrome_dragging{false};
  int m_chrome_drag_start_x{0};
  int m_chrome_drag_start_y{0};
  int m_chrome_drag_origin_x{0};
  int m_chrome_drag_origin_y{0};
  RECT m_main_bar_rect{};
  RECT m_property_bar_rect{};
};

void blitImage(HDC hdc, const Image& image, int dest_x, int dest_y);
void invalidateImageArea(const AnnotationEditorHost* data);
void destroyInlineEdit(AnnotationEditorHost* data);
bool measureAnnotationText(HDC hdc, const Annotation& annotation, SIZE& out_size);
void fillTextHitBounds(HWND hwnd, Annotation& annotation);
void commitInlineText(AnnotationEditorHost* data);
void cancelInlineText(AnnotationEditorHost* data);
void resetTextGesture(AnnotationEditorHost* data);
void clearTextSelection(AnnotationEditorHost* data);
void selectTextAnnotation(AnnotationEditorHost* data, HWND hwnd,
                          std::size_t index, int click_x, int click_y);
bool isTextDoubleClick(const AnnotationEditorHost* data, std::size_t hit,
                       int x, int y);
bool deleteSelectedTextAnnotation(AnnotationEditorHost* data);
void drawTextSelectionFrame(HDC hdc, AnnotationEditorHost* data);
void paintEditor(AnnotationEditorHost* data, HDC hdc,
                 bool draw_bar_shells = true, bool draw_bar_items = true);
void paintEditorBuffered(AnnotationEditorHost* data, HDC hdc);
void paintInlineEditFrame(HDC hdc, AnnotationEditorHost* data);
void paintLiveInlineText(HDC hdc, AnnotationEditorHost* data);
void placeInlineEditCaret(HWND edit, int caret, bool scroll_to_caret);
void layoutInlineEdit(AnnotationEditorHost* data);
void positionOwnedPopup(HWND popup, HWND owner, int client_x, int client_y,
                        int width, int height);
void applyInlineEditVisual(AnnotationEditorHost* data);
bool registerInlineEditHostClass(HINSTANCE instance);
LRESULT CALLBACK inlineEditHostWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                         LPARAM lparam);
HBRUSH ensureInlineEditKeyBrush(AnnotationEditorHost* data);
COLORREF colorBgraToRef(const ColorBgra& color);
void paintEditorToolbar(HDC hdc, AnnotationEditorHost* data, bool draw_shell);
void canvasFromClient(const AnnotationEditorHost* data, int x, int y,
                       float& out_x, float& out_y);
void paintPropertyBar(HDC hdc, AnnotationEditorHost* data, bool draw_shell);
void layoutPropertyBar(HWND hwnd, AnnotationEditorHost* data);
void layoutEditorChrome(HWND hwnd, AnnotationEditorHost* data);
void bindEditorTooltips(AnnotationEditorHost* data);
void resizeEditorChrome(AnnotationEditorHost* data);
bool hitTestChromeBar(const AnnotationEditorHost* data, int x, int y);
void beginChromeDrag(AnnotationEditorHost* data, HWND hwnd, int x, int y);
void updateChromeDrag(AnnotationEditorHost* data, int x, int y);
void endChromeDrag(AnnotationEditorHost* data);
void syncGeometryButton(AnnotationEditorHost* data);
int hitTestShapeToggle(const AnnotationEditorHost* data, int x, int y);
bool hitTestFill(const AnnotationEditorHost* data, int x, int y);
int hitTestLineStyle(const AnnotationEditorHost* data, int x, int y);
void applyLiveTextStyle(AnnotationEditorHost* data);
void syncStyleFromAnnotation(AnnotationEditorHost* data,
                            const Annotation& annotation);
int hitTestColorSwatch(const AnnotationEditorHost* data, int x, int y);
bool hitTestSizeCombo(const AnnotationEditorHost* data, int x, int y);
void applyPickedSizeValue(AnnotationEditorHost* data, bool mosaic, int value);
void pickSizeFromOverlayMenu(AnnotationEditorHost* data);
bool handleSizeComboWheel(AnnotationEditorHost* data, int delta);
bool hitTestStrokeChip(const AnnotationEditorHost* data, int x, int y);
bool strokePopupIsVisible(const AnnotationEditorHost* data);
void hideStrokePopup(AnnotationEditorHost* data);
void destroyStrokePopup(AnnotationEditorHost* data);
void showStrokePopup(AnnotationEditorHost* data);
void applyEditorStrokeWidth(AnnotationEditorHost* data, int width);
bool handleStrokeChipWheel(AnnotationEditorHost* data, int delta);
void handlePropertyBarClick(AnnotationEditorHost* data, int x, int y);
void invalidateToolbar(AnnotationEditorHost* data);
int hitTestEditorToolbar(const AnnotationEditorHost* data, int x, int y);
void handleToolCommand(AnnotationEditorHost* data, UINT id);
void handleToolbarItemClick(AnnotationEditorHost* data, UINT id);
bool createButtons(HWND hwnd, AnnotationEditorHost* data);
void fillSizeCombo(AnnotationEditorHost* data);
void syncSizeFromCombo(AnnotationEditorHost* data);
void bindSizeComboTooltip(AnnotationEditorHost* data);
void bindPropertyBarTooltips(AnnotationEditorHost* data);
void updateTextGesture(AnnotationEditorHost* data, int x, int y);
void tryPromoteInlineEditToDrag(AnnotationEditorHost* data, int client_x,
                                 int client_y);
void selectFontSizeInCombo(AnnotationEditorHost* data, int font_size);
void measureTextHitSize(HDC hdc, const Annotation& annotation, int& out_width,
                         int& out_height);
AnnotationEditorTextChrome makeTextChrome(const AnnotationEditorHost* data,
                                          const Annotation& annotation,
                                          bool use_drag_position);
std::size_t hitTestTextAnnotation(AnnotationEditorHost* data, int x, int y);
bool isCtrlZKey(WPARAM key);
LRESULT CALLBACK inlineEditSubclassProc(HWND hwnd, UINT msg, WPARAM wparam,
                                         LPARAM lparam);
void invalidateInlineEditRegion(const AnnotationEditorHost* data);
void beginInlineText(AnnotationEditorHost* data, HWND hwnd, int x, int y,
                       std::size_t edit_index);
void beginOrEditTextAt(AnnotationEditorHost* data, HWND hwnd, int x, int y);
void finishTextGesture(AnnotationEditorHost* data, HWND hwnd, int x, int y);
void paintEditorFrame(HDC hdc, AnnotationEditorHost* data);
bool colorsMatch(const ColorBgra& left, const ColorBgra& right);
RECT toWinRect(const AnnotationEditorRect& rect);
int currentStrokeWidthPx(const AnnotationEditorHost* data);
void syncStrokePopupEdit(AnnotationEditorHost* data);
void applyStrokeFromPopupEdit(AnnotationEditorHost* data);
bool handleStrokePopupEditCommand(AnnotationEditorHost* data, UINT id, UINT code,
                                  HWND control);
void applyStrokeFromPopupSlider(AnnotationEditorHost* data, int client_x);
void paintStrokePopup(HWND hwnd, AnnotationEditorHost* data);
LRESULT CALLBACK strokePopupWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                       LPARAM lparam);
bool registerStrokePopupClass(HINSTANCE instance);
RECT takeToolbarButtonRect(int& x, int y);
RECT takeToolbarSizedRect(int& x, int y, int width);
void skipToolbarDivider(int& x);
void resetPropertyBarRects(AnnotationEditorHost* data);
void refreshInlineEditFont(AnnotationEditorHost* data);
bool pointerHitsStyleChrome(const AnnotationEditorHost* data);
void requestClose(AnnotationEditorHost* data, bool confirmed);
void finishAndNotify(AnnotationEditorHost* data);
bool pointInImageArea(const AnnotationEditorHost* data, int x, int y);
RECT unionChromeRects(const RECT& main_bar, const RECT& property_bar);
void invalidateChromeMove(HWND hwnd, const RECT& old_rect,
                           const RECT& new_rect);
bool handleEditorKeyDown(HWND hwnd, AnnotationEditorHost* data, WPARAM key);
LRESULT CALLBACK editorWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                LPARAM lparam);
bool registerEditorClass(HINSTANCE instance);

}  // namespace qingying
