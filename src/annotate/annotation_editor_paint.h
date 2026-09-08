#pragma once

#include <Windows.h>

#include <optional>
#include <string>
#include <vector>

#include "annotate/annotation_editor_host.hpp"
#include "qingying/annotate/annotation_document.hpp"

namespace qingying {

// Paint receives a complete frame snapshot so rendering cannot mutate or
// observe partially updated editor state.
struct AnnotationEditorPaintSnapshot
{
  AnnotationRenderer renderer;
  Image source;
  AnnotationDocument document;
  std::optional<Annotation> preview;
  HWND overlay{nullptr};
  int image_origin_x{0};
  int image_origin_y{0};
  AnnotationTool tool{AnnotationTool::None};
  AnnotationStyle style{};
  int mosaic_block_size{DefaultMosaicBlockSize};

  EditorToolbarItem toolbar_items[kToolbarIconItemCount]{};
  RECT size_combo_rect{};
  RECT current_color_rect{};
  RECT color_swatch_rects[AnnotationStylePresetColorCount]{};
  RECT stroke_chip_rect{};
  RECT shape_rects[kGeometryShapeCount]{};
  RECT fill_rect{};
  RECT bold_rect{};
  RECT italic_rect{};
  RECT font_face_rect{};
  RECT font_menu_rect{};
  RECT arrow_style_chip_rect{};
  RECT line_style_chip_rect{};
  RECT style_menu_rect{};
  AnnotationEditorStyleMenu style_menu{AnnotationEditorStyleMenu::None};
  std::vector<std::wstring> visible_font_faces;
  int font_menu_scroll_offset{0};
  bool font_menu_open{false};
  int toolbar_hover{-1};
  bool stroke_chip_hover{false};
  bool arrow_style_chip_hover{false};
  bool line_style_chip_hover{false};
  bool bold_hover{false};
  bool italic_hover{false};
  bool font_face_hover{false};
  int toolbar_divider_x[AnnotationEditorDividerCount]{};
  RECT main_bar_rect{};
  RECT property_bar_rect{};
  HFONT combo_font{nullptr};
  bool color_picker_visible{false};
  bool stroke_popup_visible{false};

  bool text_dragging{false};
  std::size_t text_target_index{kInvalidAnnotationIndex};
  float text_drag_x{0.0f};
  float text_drag_y{0.0f};
  float text_anchor_x{0.0f};
  float text_anchor_y{0.0f};
  bool inline_edit_visible{false};
  std::size_t editing_text_index{kInvalidAnnotationIndex};
  std::size_t selected_text_index{kInvalidAnnotationIndex};
  std::wstring inline_text;
  int inline_caret{0};
  RECT inline_edit_rect{};
};

AnnotationEditorPaintSnapshot makeAnnotationEditorPaintSnapshot(
    const AnnotationEditorHost& data);
void blitImage(HDC hdc, const Image& image, int dest_x, int dest_y);
void drawTextSelectionFrame(HDC hdc,
                            const AnnotationEditorPaintSnapshot& snapshot);
void paintEditor(const AnnotationEditorPaintSnapshot& snapshot, HDC hdc,
                 bool draw_bar_shells = true, bool draw_bar_items = true);
void paintEditorBuffered(const AnnotationEditorPaintSnapshot& snapshot, HDC hdc);
void paintEditorFrame(HDC hdc,
                      const AnnotationEditorPaintSnapshot& snapshot);

}  // namespace qingying
