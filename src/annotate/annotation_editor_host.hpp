#pragma once

#include <Windows.h>

#include <cstddef>

#include "annotate/gdi_raii.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/annotate/annotation_font_catalog.hpp"
#include "qingying/annotate/annotation_editor_layout.hpp"
#include "qingying/annotate/annotation_editor_session.hpp"
#include "qingying/annotate/annotation_interaction_controller.hpp"
#include "qingying/annotate/annotation_renderer.hpp"
#include "qingying/annotate/annotation_result.hpp"
#include "qingying/annotate/annotation_types.hpp"
#include "qingying/annotate/color_picker_state.hpp"
#include "qingying/ui/modern_toolbar.hpp"

namespace qingying {

inline constexpr wchar_t kOverlayClassName[] = L"QingYingAnnotationOverlay";
inline constexpr wchar_t kStrokePopupClassName[] = L"QingYingStrokePopup";
inline constexpr wchar_t kColorPickerClassName[] = L"QingYingColorPicker";
inline constexpr wchar_t kColorPickerEyedropperSurfaceClassName[] =
    L"QingYingColorPickerEyedropperSurface";
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
inline constexpr UINT kButtonRedoId = 11;
inline constexpr UINT kButtonGeometryId = 23;
inline constexpr UINT kButtonMoveId = 24;
inline constexpr UINT kInlineEditId = 10;
inline constexpr UINT kFontComboId = 20;
inline constexpr UINT kStrokePopupEditId = 21;
inline constexpr UINT_PTR kStrokePopupEditSubclassId = 1;
inline constexpr UINT kColorPickerFormatComboId = 30;
inline constexpr UINT kColorPickerHexEditId = 31;
inline constexpr UINT kColorPickerChannel0Id = 32;
inline constexpr UINT kColorPickerChannel1Id = 33;
inline constexpr UINT kColorPickerChannel2Id = 34;
inline constexpr UINT kColorPickerAlphaEditId = 35;
inline constexpr UINT kColorPickerOkId = 36;
inline constexpr UINT kColorPickerCancelId = 37;
inline constexpr UINT_PTR kColorPickerEditSubclassId = 2;
inline constexpr UINT kSizeMenuBaseId = 400;
inline constexpr UINT kTipShapeRectId = 200;
inline constexpr UINT kTipShapeEllipseId = 201;
inline constexpr UINT kTipFillId = 202;
inline constexpr UINT kTipArrowStyleId = 209;
inline constexpr UINT kTipLineStyleId = 210;
inline constexpr UINT kTipStrokeId = 220;
inline constexpr UINT kTipCurrentColorId = 229;
inline constexpr UINT kTipColorBaseId = 230;
inline constexpr UINT kTipLineStyleItemBaseId = 240;
inline constexpr UINT kTipArrowStyleItemBaseId =
    kTipLineStyleItemBaseId + AnnotationLineStyleCount;
inline constexpr int kComboFontPx = 13;

inline constexpr int kInlineTextMaxChars = 256;
inline constexpr int kTextDragThresholdPx = 4;
inline constexpr int kTextHitPaddingPx = 10;
inline constexpr int kTextMinHitWidthPx = 28;
inline constexpr int kTextMinHitHeightPx = 20;
inline constexpr std::size_t kInvalidAnnotationIndex =
    static_cast<std::size_t>(-1);

inline constexpr int kToolbarIconItemCount = 10;
inline constexpr int kGeometryShapeCount = 2;
inline constexpr int kComboTooltipSlot = kToolbarIconItemCount;
inline constexpr int kShapeTooltipSlot = kComboTooltipSlot + 1;
inline constexpr int kFillTooltipSlot = kShapeTooltipSlot + kGeometryShapeCount;
inline constexpr int kLineStyleTooltipSlot = kFillTooltipSlot + 1;
inline constexpr int kArrowStyleTooltipSlot = kLineStyleTooltipSlot + 1;
inline constexpr int kStrokeTooltipSlot = kArrowStyleTooltipSlot + 1;
inline constexpr int kCurrentColorTooltipSlot = kStrokeTooltipSlot + 1;
inline constexpr int kColorTooltipSlot = kCurrentColorTooltipSlot + 1;
inline constexpr int kStyleMenuTooltipSlot =
    kColorTooltipSlot + AnnotationStylePresetColorCount;
inline constexpr int kTooltipSlotCount =
    kStyleMenuTooltipSlot +
    (AnnotationLineStyleCount > AnnotationArrowStyleCount
         ? AnnotationLineStyleCount
         : AnnotationArrowStyleCount);
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
static_assert(static_cast<COLORREF>(AnnotationEditorInlineEditColorKeyRgb) ==
                  kToolbarColorKey,
              "inline edit color-key must match toolbar punch-through key");
inline constexpr COLORREF kTextDeleteFillColor = RGB(220, 56, 48);
inline constexpr COLORREF kTextDeleteGlyphColor = RGB(255, 255, 255);
inline constexpr int kTextDeleteGlyphWidthPx = 2;
inline constexpr int kTextDeleteGlyphInsetPx = 4;
inline constexpr int kSizeLabelFontPx = 12;
inline constexpr int kSizeLabelPadX = 6;
inline constexpr int kSizeLabelPadY = 2;
inline constexpr int kChromeInvalidateExtraPadPx = 2;
inline constexpr int kInlineCaretWidthPx = 1;
struct EditorToolbarItem
{
  UINT id{0};
  ToolbarIconKind icon{ToolbarIconKind::Rectangle};
  bool accent{false};
  RECT rect{};
};

struct AnnotationEditorCoreState
{
  AnnotationEditorSession m_session;
  AnnotationInteractionController m_controller;
  AnnotationRenderer m_renderer;
};

struct AnnotationEditorWindowState
{
  AnnotationCallback m_callback;
  HWND m_overlay{nullptr};
  bool m_confirmed{false};
  int m_client_width{0};
  HWND* m_owner_hwnd{nullptr};
  bool* m_owner_visible{nullptr};
  bool* m_owner_suppress_callback{nullptr};
  bool m_window_destroyed{false};
  int m_image_origin_x{AnnotationEditorFrameInsetPx};
  int m_image_origin_y{0};
  int m_image_screen_x{0};
  int m_image_screen_y{0};
};

struct AnnotationEditorInlineTextState
{
  HWND m_inline_edit{nullptr};
  HWND m_inline_edit_host{nullptr};
  WNDPROC m_inline_edit_prev_proc{nullptr};
  GdiObject m_inline_edit_font;
  GdiObject m_inline_edit_key_brush;
  float m_text_anchor_x{0.0f};
  float m_text_anchor_y{0.0f};
  float m_text_wrap_width{0.0f};
  float m_text_max_width{0.0f};
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
  bool m_text_rotating{false};
  std::size_t m_text_target_index{kInvalidAnnotationIndex};
  float m_text_press_x{0.0f};
  float m_text_press_y{0.0f};
  float m_text_origin_x{0.0f};
  float m_text_origin_y{0.0f};
  float m_text_drag_x{0.0f};
  float m_text_drag_y{0.0f};
  float m_text_rotation_degrees{0.0f};
  float m_text_rotation_initial_degrees{0.0f};
  float m_text_rotation_press_degrees{0.0f};
};

struct AnnotationEditorChromeState
{
  HWND m_font_combo{nullptr};
  GdiObject m_combo_font;
  bool m_size_combo_syncing{false};
  RECT m_size_combo_rect{};
  EditorToolbarItem m_toolbar_items[kToolbarIconItemCount]{};
  RECT m_current_color_rect{};
  RECT m_color_swatch_rects[AnnotationStylePresetColorCount]{};
  RECT m_stroke_chip_rect{};
  RECT m_shape_rects[kGeometryShapeCount]{};
  RECT m_fill_rect{};
  RECT m_bold_rect{};
  RECT m_italic_rect{};
  RECT m_font_face_rect{};
  RECT m_font_menu_rect{};
  RECT m_arrow_style_chip_rect{};
  RECT m_line_style_chip_rect{};
  RECT m_style_menu_rect{};
  AnnotationEditorStyleMenu m_style_menu{AnnotationEditorStyleMenu::None};
  AnnotationFontCatalog m_font_catalog;
  int m_font_menu_scroll_offset{0};
  bool m_font_menu_open{false};
  AnnotationTool m_last_geometry_tool{AnnotationTool::Rectangle};
  int m_toolbar_hover{-1};
  bool m_stroke_chip_hover{false};
  bool m_arrow_style_chip_hover{false};
  bool m_line_style_chip_hover{false};
  bool m_bold_hover{false};
  bool m_italic_hover{false};
  bool m_font_face_hover{false};
  int m_toolbar_divider_x[AnnotationEditorDividerCount]{};
  HWND m_tooltip{nullptr};
  wchar_t m_tooltip_text[kTooltipSlotCount][kToolbarTooltipMaxChars]{};
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

struct AnnotationEditorStrokePopupState
{
  HWND m_stroke_popup{nullptr};
  HWND m_stroke_popup_edit{nullptr};
  bool m_stroke_syncing{false};
  bool m_stroke_slider_dragging{false};
};

struct AnnotationEditorColorPickerState
{
  ColorPickerState m_color_picker_state;
  HWND m_color_picker{nullptr};
  HWND m_color_format_combo{nullptr};
  HWND m_color_hex_edit{nullptr};
  HWND m_color_channel_edits[3]{};
  HWND m_color_alpha_edit{nullptr};
  HWND m_color_ok{nullptr};
  HWND m_color_cancel{nullptr};
  bool m_color_picker_syncing{false};
  bool m_color_picker_dragging{false};
  bool m_color_picker_moved{false};
  bool m_color_picker_eyedropping{false};
  HWND m_color_picker_eyedropper_surface{nullptr};
  Image m_color_picker_eyedropper_snapshot;
  int m_color_picker_eyedropper_snapshot_x{0};
  int m_color_picker_eyedropper_snapshot_y{0};
  bool m_color_sv_dragging{false};
  bool m_color_hue_dragging{false};
  bool m_color_alpha_dragging{false};
  int m_color_picker_drag_start_x{0};
  int m_color_picker_drag_start_y{0};
  int m_color_picker_drag_origin_x{0};
  int m_color_picker_drag_origin_y{0};
  int m_color_picker_screen_x{0};
  int m_color_picker_screen_y{0};
};

// 标注编辑器只对外暴露行为；各编译单元通过这些状态访问器获得自己需要的窄上下文。
// 所有权由 AnnotationOverlay 的 unique_ptr 持有，GWLP_USERDATA 只保存观察指针。
class AnnotationEditorHost
{
 public:
  AnnotationEditorHost() = default;
  ~AnnotationEditorHost() = default;

  AnnotationEditorHost(const AnnotationEditorHost&) = delete;
  AnnotationEditorHost& operator=(const AnnotationEditorHost&) = delete;

  AnnotationEditorCoreState& core() noexcept { return core_state_; }
  const AnnotationEditorCoreState& core() const noexcept { return core_state_; }
  AnnotationEditorWindowState& window() noexcept { return window_state_; }
  const AnnotationEditorWindowState& window() const noexcept {
    return window_state_;
  }
  AnnotationEditorInlineTextState& inlineText() noexcept {
    return inline_text_state_;
  }
  const AnnotationEditorInlineTextState& inlineText() const noexcept {
    return inline_text_state_;
  }
  AnnotationEditorChromeState& chrome() noexcept { return chrome_state_; }
  const AnnotationEditorChromeState& chrome() const noexcept {
    return chrome_state_;
  }
  AnnotationEditorStrokePopupState& strokePopup() noexcept {
    return stroke_popup_state_;
  }
  const AnnotationEditorStrokePopupState& strokePopup() const noexcept {
    return stroke_popup_state_;
  }
  AnnotationEditorColorPickerState& colorPicker() noexcept {
    return color_picker_state_;
  }
  const AnnotationEditorColorPickerState& colorPicker() const noexcept {
    return color_picker_state_;
  }

 private:
  AnnotationEditorCoreState core_state_;
  AnnotationEditorWindowState window_state_;
  AnnotationEditorInlineTextState inline_text_state_;
  AnnotationEditorChromeState chrome_state_;
  AnnotationEditorStrokePopupState stroke_popup_state_;
  AnnotationEditorColorPickerState color_picker_state_;
};

void invalidateImageArea(const AnnotationEditorHost* data);
void canvasFromClient(const AnnotationEditorHost* data, int x, int y,
                       float& out_x, float& out_y);
void requestClose(AnnotationEditorHost* data, bool confirmed);
void finishAndNotify(AnnotationEditorHost* data);
bool pointInImageArea(const AnnotationEditorHost* data, int x, int y);
bool handleEditorKeyDown(HWND hwnd, AnnotationEditorHost* data, WPARAM key);
LRESULT CALLBACK editorWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                LPARAM lparam);
bool registerEditorClass(HINSTANCE instance);

}  // namespace qingying
