#include "annotate/annotation_editor_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

#include "annotate/annotation_editor_paint.h"

#include <commctrl.h>

#include "annotate/annotation_editor_chrome.h"
#include "annotate/annotation_editor_color_picker.h"
#include "annotate/annotation_editor_inline_text.h"
#include "annotate/annotation_editor_stroke_popup.h"
#include "annotate/annotation_text_rasterizer.h"

namespace qingying {

void fillSizeCombo(AnnotationEditorHost* data)
{
  if (data == nullptr || data->chrome().m_font_combo == nullptr)
  {
    return;
  }

  const bool mosaic =
      annotationEditorPropertyBarShowsMosaicSize(data->core().m_controller.tool());
  const int* options = mosaic ? AnnotationEditorMosaicSizeOptions
                              : AnnotationEditorFontSizeOptions;
  const int count = mosaic ? AnnotationEditorMosaicSizeOptionCount
                           : AnnotationEditorFontSizeOptionCount;
  const int current = mosaic ? data->core().m_controller.mosaicBlockSize()
                             : data->core().m_controller.style().font_size;

  data->chrome().m_size_combo_syncing = true;
  SendMessageW(data->chrome().m_font_combo, CB_RESETCONTENT, 0, 0);
  int selected = 0;
  for (int i = 0; i < count; ++i)
  {
    wchar_t label[16]{};
    swprintf_s(label, L"%d", options[i]);
    SendMessageW(data->chrome().m_font_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(label));
    if (options[i] == current)
    {
      selected = i;
    }
  }
  SendMessageW(data->chrome().m_font_combo, CB_SETCURSEL, static_cast<WPARAM>(selected),
               0);
  data->chrome().m_size_combo_syncing = false;
}

void bindSizeComboTooltip(AnnotationEditorHost* data)
{
  if (data == nullptr || data->chrome().m_tooltip == nullptr || data->window().m_overlay == nullptr)
  {
    return;
  }

  RECT local = data->chrome().m_size_combo_rect;
  const bool show_combo =
      annotationEditorPropertyBarPaintsSizeCombo(data->core().m_controller.tool()) &&
      local.right > local.left;

  const wchar_t* tip = L"";
  if (show_combo)
  {
    tip = annotationEditorPropertyBarShowsMosaicSize(data->core().m_controller.tool())
              ? L"\x5757\x5927\x5C0F"
              : L"\x5B57\x53F7";
  }
  bindToolbarTooltip(data->chrome().m_tooltip, data->window().m_overlay, kFontComboId, local, tip,
                     data->chrome().m_tooltip_text[kComboTooltipSlot],
                     kToolbarTooltipMaxChars);
}

void bindPropertyBarTooltips(AnnotationEditorHost* data)
{
  if (data == nullptr || data->chrome().m_tooltip == nullptr || data->window().m_overlay == nullptr)
  {
    return;
  }

  const HWND overlay = data->window().m_overlay;
  const HWND tooltip = data->chrome().m_tooltip;
  bindToolbarTooltip(tooltip, overlay, kTipShapeRectId, data->chrome().m_shape_rects[0],
                     toolbarIconLabel(ToolbarIconKind::Rectangle),
                     data->chrome().m_tooltip_text[kShapeTooltipSlot],
                     kToolbarTooltipMaxChars);
  bindToolbarTooltip(tooltip, overlay, kTipShapeEllipseId, data->chrome().m_shape_rects[1],
                     toolbarIconLabel(ToolbarIconKind::Ellipse),
                     data->chrome().m_tooltip_text[kShapeTooltipSlot + 1],
                     kToolbarTooltipMaxChars);
  bindToolbarTooltip(tooltip, overlay, kTipFillId, data->chrome().m_fill_rect,
                     toolbarIconLabel(ToolbarIconKind::Fill),
                     data->chrome().m_tooltip_text[kFillTooltipSlot],
                     kToolbarTooltipMaxChars);

  bindToolbarTooltip(tooltip, overlay, kTipLineStyleId,
                     data->chrome().m_line_style_chip_rect,
                     L"\x7EBF\x6761\x6837\x5F0F",
                     data->chrome().m_tooltip_text[kLineStyleTooltipSlot],
                     kToolbarTooltipMaxChars);
  bindToolbarTooltip(tooltip, overlay, kTipArrowStyleId,
                     data->chrome().m_arrow_style_chip_rect,
                     L"\x7BAD\x5934\x6837\x5F0F",
                     data->chrome().m_tooltip_text[kArrowStyleTooltipSlot],
                     kToolbarTooltipMaxChars);

  bindToolbarTooltip(tooltip, overlay, kTipStrokeId, data->chrome().m_stroke_chip_rect,
                     toolbarIconLabel(ToolbarIconKind::StrokeWidth),
                     data->chrome().m_tooltip_text[kStrokeTooltipSlot],
                     kToolbarTooltipMaxChars);

  bindToolbarTooltip(tooltip, overlay, kTipCurrentColorId,
                     data->chrome().m_current_color_rect,
                     L"\x5F53\x524D\x989C\x8272",
                     data->chrome().m_tooltip_text[kCurrentColorTooltipSlot],
                     kToolbarTooltipMaxChars);

  for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
  {
    bindToolbarTooltip(tooltip, overlay, kTipColorBaseId + static_cast<UINT>(i),
                       data->chrome().m_color_swatch_rects[static_cast<std::size_t>(i)],
                       toolbarColorPresetLabel(i),
                       data->chrome().m_tooltip_text[kColorTooltipSlot + i],
                       kToolbarTooltipMaxChars);
  }

  bindSizeComboTooltip(data);

  const bool arrow_menu =
      data->chrome().m_style_menu == AnnotationEditorStyleMenu::Arrow;
  const bool line_menu =
      data->chrome().m_style_menu == AnnotationEditorStyleMenu::Line;
  const int style_count = arrow_menu
                              ? AnnotationArrowStyleCount
                              : (line_menu ? AnnotationLineStyleCount : 0);
  const AnnotationEditorRect style_menu{
      data->chrome().m_style_menu_rect.left,
      data->chrome().m_style_menu_rect.top,
      data->chrome().m_style_menu_rect.right,
      data->chrome().m_style_menu_rect.bottom};
  const int max_style_count =
      (std::max)(AnnotationLineStyleCount, AnnotationArrowStyleCount);
  for (int i = 0; i < max_style_count; ++i)
  {
    RECT item{};
    const wchar_t* label = L"";
    UINT id = kTipLineStyleItemBaseId + static_cast<UINT>(i);
    if (arrow_menu)
    {
      id = kTipArrowStyleItemBaseId + static_cast<UINT>(i);
      label = toolbarArrowStyleLabel(i);
    }
    else if (line_menu)
    {
      label = toolbarLineStyleLabel(i);
    }
    if (i < style_count)
    {
      item = toWinRect(annotationEditorStyleMenuItemRect(style_menu, i));
    }
    bindToolbarTooltip(tooltip, overlay, id, item, label,
                       data->chrome().m_tooltip_text[kStyleMenuTooltipSlot + i],
                       kToolbarTooltipMaxChars);
  }
}

void syncSizeFromCombo(AnnotationEditorHost* data)
{
  if (data == nullptr || data->chrome().m_font_combo == nullptr ||
      data->chrome().m_size_combo_syncing)
  {
    return;
  }

  const LRESULT index = SendMessageW(data->chrome().m_font_combo, CB_GETCURSEL, 0, 0);
  if (index == CB_ERR || index < 0)
  {
    return;
  }

  if (annotationEditorPropertyBarShowsMosaicSize(data->core().m_controller.tool()))
  {
    if (index >= AnnotationEditorMosaicSizeOptionCount)
    {
      return;
    }
    data->core().m_controller.setMosaicBlockSize(
        AnnotationEditorMosaicSizeOptions[static_cast<std::size_t>(index)]);
    if (data->core().m_controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    return;
  }

  if (index >= AnnotationEditorFontSizeOptionCount)
  {
    return;
  }

  data->core().m_controller.setFontSize(
      AnnotationEditorFontSizeOptions[static_cast<std::size_t>(index)]);
  if (data->inlineText().m_inline_edit != nullptr)
  {
    const AnnotationStyle& style = data->core().m_controller.style();
    data->inlineText().m_inline_edit_font =
        createAnnotationTextFont(style, style.font_size);
    if (data->inlineText().m_inline_edit_font)
    {
      SendMessageW(data->inlineText().m_inline_edit, WM_SETFONT,
                   reinterpret_cast<WPARAM>(data->inlineText().m_inline_edit_font.get()),
                   TRUE);
    }
    applyInlineEditVisual(data);
  }
  if (data->inlineText().m_inline_edit != nullptr)
  {
    SetFocus(data->inlineText().m_inline_edit);
  }
  applyLiveTextStyle(data);
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

void syncStyleFromAnnotation(AnnotationEditorHost* data,
                             const Annotation& annotation)
{
  if (data == nullptr)
  {
    return;
  }
  data->core().m_controller.setColor(annotation.style.color);
  data->core().m_controller.setFontFace(annotation.style.font_face);
  selectFontSizeInCombo(data, annotation.style.font_size);
  data->core().m_controller.setBold(annotation.style.bold);
  data->core().m_controller.setItalic(annotation.style.italic);
  invalidateToolbar(data);
}

void applyLiveTextStyle(AnnotationEditorHost* data)
{
  if (data == nullptr || data->window().m_overlay == nullptr)
  {
    return;
  }

  applyInlineEditVisual(data);

  const std::size_t index = annotationEditorTextStyleTargetIndex(
      data->inlineText().m_selected_text_index, data->inlineText().m_editing_text_index,
      data->core().m_session.engine().document().count());
  if (index == AnnotationEditorInvalidIndex)
  {
    return;
  }

  Annotation updated = data->core().m_session.engine().document().items().at(index);
  if (updated.type != AnnotationType::Text)
  {
    return;
  }

  updated.style.color = data->core().m_controller.style().color;
  updated.style.font_face = data->core().m_controller.style().font_face;
  updated.style.font_size = data->core().m_controller.style().font_size;
  updated.style.bold = data->core().m_controller.style().bold;
  updated.style.italic = data->core().m_controller.style().italic;
  fillTextHitBounds(data->window().m_overlay, updated);
  if (data->core().m_session.engine().replaceAt(index, updated))
  {
    invalidateImageArea(data);
  }
}

RECT toWinRect(const AnnotationEditorRect& rect)
{
  return RECT{rect.left, rect.top, rect.right, rect.bottom};
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

void resetPropertyBarRects(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }

  for (int i = 0; i < kGeometryShapeCount; ++i)
  {
    data->chrome().m_shape_rects[static_cast<std::size_t>(i)] = {};
  }
  data->chrome().m_fill_rect = {};
  data->chrome().m_bold_rect = {};
  data->chrome().m_italic_rect = {};
  data->chrome().m_font_face_rect = {};
  data->chrome().m_font_menu_rect = {};
  data->chrome().m_arrow_style_chip_rect = {};
  data->chrome().m_line_style_chip_rect = {};
  data->chrome().m_style_menu_rect = {};
  data->chrome().m_stroke_chip_rect = {};
  data->chrome().m_size_combo_rect = {};
  data->chrome().m_current_color_rect = {};
  for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
  {
    data->chrome().m_color_swatch_rects[static_cast<std::size_t>(i)] = {};
  }
}

void syncGeometryButton(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  if (annotationEditorIsGeometryTool(data->core().m_controller.tool()))
  {
    data->chrome().m_last_geometry_tool = data->core().m_controller.tool();
  }
  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    if (data->chrome().m_toolbar_items[static_cast<std::size_t>(i)].id != kButtonGeometryId)
    {
      continue;
    }
    data->chrome().m_toolbar_items[static_cast<std::size_t>(i)].icon =
        (data->chrome().m_last_geometry_tool == AnnotationTool::Ellipse)
            ? ToolbarIconKind::Ellipse
            : ToolbarIconKind::Rectangle;
    break;
  }
}

void layoutEditorChrome(HWND hwnd, AnnotationEditorHost* data)
{
  if (hwnd == nullptr || data == nullptr)
  {
    return;
  }

  const Image& source = data->core().m_session.source();
  const int default_x = data->window().m_image_screen_x;
  const int default_y =
      data->window().m_image_screen_y + source.height + AnnotationEditorChromeImageGap;
  const int main_width = annotationEditorMainToolbarWidth();
  const int property_width =
      annotationEditorShowsPropertyBar(data->core().m_controller.tool())
          ? annotationEditorPropertyBarWidth()
          : 0;
  const int span_width =
      annotationEditorChromeSpanWidth(main_width, property_width);
  const int chrome_height =
      annotationEditorChromeHeight(data->core().m_controller.tool()) -
      AnnotationEditorChromeImageGap;
  const int screen_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int screen_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int screen_right = screen_left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int screen_bottom = screen_top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  annotationEditorClampChromeOffset(data->chrome().m_chrome_offset_x, data->chrome().m_chrome_offset_y,
                                    default_x, default_y, span_width,
                                    chrome_height, screen_left, screen_top,
                                    screen_right, screen_bottom);

  const int chrome_screen_x = default_x + data->chrome().m_chrome_offset_x;
  const int chrome_screen_y = default_y + data->chrome().m_chrome_offset_y;
  // 窗口已铺满虚拟屏，禁止再 SetWindowPos：拖栏时改窗口原点会让截图框先跟着走再被重绘拉回，边缘处明显抖动。
  const int bar_left = chrome_screen_x - screen_left;
  const int bar_top = chrome_screen_y - screen_top;
  const int bar_height = annotationEditorToolbarHeight();
  data->chrome().m_main_bar_rect = {bar_left, bar_top, bar_left + main_width,
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
    data->chrome().m_toolbar_items[static_cast<std::size_t>(item_index)].id = spec.id;
    data->chrome().m_toolbar_items[static_cast<std::size_t>(item_index)].icon = spec.icon;
    data->chrome().m_toolbar_items[static_cast<std::size_t>(item_index)].accent = spec.accent;
    data->chrome().m_toolbar_items[static_cast<std::size_t>(item_index)].rect = {
        x, y, x + AnnotationEditorButtonWidth, y + AnnotationEditorButtonHeight};
    x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
    ++item_index;
  }

  data->chrome().m_toolbar_divider_x[0] =
      x - AnnotationEditorButtonGap + AnnotationEditorDividerGap / 2;
  x += AnnotationEditorDividerGap - AnnotationEditorButtonGap;

  data->chrome().m_toolbar_items[static_cast<std::size_t>(item_index)] = {
      kButtonUndoId,
      ToolbarIconKind::Undo,
      false,
      {x, y, x + AnnotationEditorButtonWidth, y + AnnotationEditorButtonHeight}};
  ++item_index;
  x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  data->chrome().m_toolbar_items[static_cast<std::size_t>(item_index)] = {
      kButtonRedoId,
      ToolbarIconKind::Redo,
      false,
      {x, y, x + AnnotationEditorButtonWidth, y + AnnotationEditorButtonHeight}};
  ++item_index;
  x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  data->chrome().m_toolbar_divider_x[1] =
      x - AnnotationEditorButtonGap + AnnotationEditorDividerGap / 2;
  x += AnnotationEditorDividerGap - AnnotationEditorButtonGap;

  const int action_total =
      AnnotationEditorButtonWidth * 2 + AnnotationEditorButtonGap;
  const int confirm_x =
      (std::max)(x, bar_left + main_width - action_total -
                        AnnotationEditorBarPadding);

  data->chrome().m_toolbar_items[static_cast<std::size_t>(item_index)] = {
      kButtonConfirmId,
      ToolbarIconKind::Confirm,
      false,
      {confirm_x, y, confirm_x + AnnotationEditorButtonWidth,
       y + AnnotationEditorButtonHeight}};
  ++item_index;

  const int cancel_x =
      confirm_x + AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  data->chrome().m_toolbar_items[static_cast<std::size_t>(item_index)] = {
      kButtonCancelId,
      ToolbarIconKind::Cancel,
      false,
      {cancel_x, y, cancel_x + AnnotationEditorButtonWidth,
       y + AnnotationEditorButtonHeight}};

  layoutPropertyBar(hwnd, data);
  syncGeometryButton(data);
  bindEditorTooltips(data);
  if (data->inlineText().m_inline_edit != nullptr)
  {
    layoutInlineEdit(data);
  }
}

void layoutPropertyBar(HWND hwnd, AnnotationEditorHost* data)
{
  if (hwnd == nullptr || data == nullptr)
  {
    return;
  }

  data->chrome().m_property_bar_rect = {};
  const AnnotationTool tool = data->core().m_controller.tool();
  resetPropertyBarRects(data);
  if (!annotationEditorShowsPropertyBar(tool))
  {
    if (data->chrome().m_font_combo != nullptr)
    {
      ShowWindow(data->chrome().m_font_combo, SW_HIDE);
    }
    return;
  }

  const int y = data->chrome().m_main_bar_rect.bottom + AnnotationEditorChromeStackGap +
                AnnotationEditorBarPadding;
  const int bar_left = data->chrome().m_main_bar_rect.left;
  const int bar_top = data->chrome().m_main_bar_rect.bottom + AnnotationEditorChromeStackGap;
  const int swatch = AnnotationEditorColorSwatchSize;
  const int swatch_y = y + (AnnotationEditorButtonHeight - swatch) / 2;
  int x = bar_left + AnnotationEditorBarPadding;
  const int mosaic_combo_x = x;

  if (annotationEditorPropertyBarShowsTextStyle(tool))
  {
    data->chrome().m_bold_rect = takeToolbarButtonRect(x, y);
    data->chrome().m_italic_rect = takeToolbarButtonRect(x, y);
    skipToolbarDivider(x);
    data->chrome().m_font_face_rect =
        takeToolbarSizedRect(x, y, AnnotationEditorFontFaceChipWidth);
    skipToolbarDivider(x);
    data->chrome().m_size_combo_rect =
        takeToolbarSizedRect(x, y, AnnotationEditorFontComboWidth);
    skipToolbarDivider(x);
  }

  if (annotationEditorPropertyBarShowsShapeToggle(tool))
  {
    data->chrome().m_shape_rects[0] = takeToolbarButtonRect(x, y);
    data->chrome().m_shape_rects[1] = takeToolbarButtonRect(x, y);
    skipToolbarDivider(x);
  }
  if (annotationEditorPropertyBarShowsFill(tool))
  {
    data->chrome().m_fill_rect = takeToolbarButtonRect(x, y);
    skipToolbarDivider(x);
  }
  if (annotationEditorPropertyBarShowsArrowStyle(tool))
  {
    data->chrome().m_arrow_style_chip_rect =
        takeToolbarSizedRect(x, y, AnnotationEditorStyleChipWidth);
    skipToolbarDivider(x);
  }
  if (annotationEditorPropertyBarShowsLineStyle(tool))
  {
    data->chrome().m_line_style_chip_rect =
        takeToolbarSizedRect(x, y, AnnotationEditorStyleChipWidth);
    skipToolbarDivider(x);
  }

  if (annotationEditorPropertyBarShowsStroke(tool) &&
      (annotationEditorIsGeometryTool(tool) || tool == AnnotationTool::Arrow))
  {
    data->chrome().m_stroke_chip_rect =
        takeToolbarSizedRect(x, y, AnnotationEditorStrokeChipWidth);
    skipToolbarDivider(x);
  }

  int combo_x = x;
  if (annotationEditorPropertyBarShowsColor(tool))
  {
    const int current = AnnotationEditorCurrentColorSwatchSize;
    data->chrome().m_current_color_rect = {x, swatch_y, x + current, swatch_y + current};
    x += current + AnnotationEditorButtonGap;
    for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
    {
      data->chrome().m_color_swatch_rects[static_cast<std::size_t>(i)] = {
          x, swatch_y, x + swatch, swatch_y + swatch};
      x += swatch + AnnotationEditorButtonGap;
    }
    x += AnnotationEditorDividerGap - AnnotationEditorButtonGap;
    combo_x = x;
  }

  if (annotationEditorPropertyBarShowsStroke(tool) &&
      !annotationEditorIsGeometryTool(tool) && tool != AnnotationTool::Arrow)
  {
    combo_x = x;
    data->chrome().m_stroke_chip_rect =
        takeToolbarSizedRect(x, y, AnnotationEditorStrokeChipWidth);
  }

  if (!annotationEditorPropertyBarShowsTextStyle(tool))
  {
    data->chrome().m_size_combo_rect = {};
  }
  if (annotationEditorPropertyBarShowsSizeCombo(tool) &&
      !annotationEditorPropertyBarShowsTextStyle(tool))
  {
    const int placed_x =
        annotationEditorPropertyBarShowsMosaicSize(tool) ? mosaic_combo_x
                                                         : combo_x;
    data->chrome().m_size_combo_rect = {placed_x, y,
                             placed_x + AnnotationEditorFontComboWidth,
                             y + AnnotationEditorButtonHeight};
    x = (std::max)(x, placed_x + AnnotationEditorFontComboWidth +
                          AnnotationEditorButtonGap);
  }
  if (data->chrome().m_font_combo != nullptr)
  {
    ShowWindow(data->chrome().m_font_combo, SW_HIDE);
  }

  const int content_right =
      (std::max)(x - AnnotationEditorButtonGap,
                 bar_left + AnnotationEditorBarPadding);
  data->chrome().m_property_bar_rect = {
      bar_left, bar_top,
      content_right + AnnotationEditorBarPadding,
      bar_top + annotationEditorToolbarHeight()};

  const RECT style_chip =
      data->chrome().m_style_menu == AnnotationEditorStyleMenu::Arrow
          ? data->chrome().m_arrow_style_chip_rect
          : data->chrome().m_line_style_chip_rect;
  const int style_count =
      data->chrome().m_style_menu == AnnotationEditorStyleMenu::Arrow
          ? AnnotationArrowStyleCount
          : AnnotationLineStyleCount;
  if (data->chrome().m_style_menu != AnnotationEditorStyleMenu::None &&
      style_chip.right > style_chip.left)
  {
    const AnnotationEditorRect chip_rect{style_chip.left, style_chip.top,
                                          style_chip.right, style_chip.bottom};
    AnnotationEditorRect menu =
        annotationEditorStyleMenuRect(chip_rect, style_count);
    RECT client{};
    GetClientRect(hwnd, &client);
    if (menu.bottom > client.bottom)
    {
      const int height = menu.bottom - menu.top;
      menu.bottom = style_chip.top - AnnotationEditorButtonGap;
      menu.top = menu.bottom - height;
    }
    data->chrome().m_style_menu_rect = toWinRect(menu);
  }

  if (data->chrome().m_font_menu_open &&
      data->chrome().m_font_face_rect.right >
          data->chrome().m_font_face_rect.left)
  {
    const std::vector<std::wstring>& fonts =
        data->chrome().m_font_catalog.cachedFonts();
    const AnnotationEditorRect chip_rect{
        data->chrome().m_font_face_rect.left,
        data->chrome().m_font_face_rect.top,
        data->chrome().m_font_face_rect.right,
        data->chrome().m_font_face_rect.bottom};
    AnnotationEditorRect menu = annotationEditorFontMenuRect(
        chip_rect, static_cast<int>(fonts.size()));
    RECT client{};
    GetClientRect(hwnd, &client);
    if (menu.bottom > client.bottom)
    {
      const int height = menu.bottom - menu.top;
      menu.bottom = chip_rect.top - AnnotationEditorButtonGap;
      menu.top = menu.bottom - height;
    }
    if (menu.right > client.right)
    {
      const int width = menu.right - menu.left;
      menu.right = client.right;
      menu.left = menu.right - width;
    }
    data->chrome().m_font_menu_rect = toWinRect(menu);
  }

  bindPropertyBarTooltips(data);
}

void resizeEditorChrome(AnnotationEditorHost* data)
{
  if (data == nullptr || data->window().m_overlay == nullptr)
  {
    return;
  }

  const AnnotationTool tool = data->core().m_controller.tool();
  if (!annotationEditorPropertyBarShowsFontFace(tool))
  {
    closeFontMenu(data);
  }
  if ((data->chrome().m_style_menu == AnnotationEditorStyleMenu::Arrow &&
       !annotationEditorPropertyBarShowsArrowStyle(tool)) ||
      (data->chrome().m_style_menu == AnnotationEditorStyleMenu::Line &&
       !annotationEditorPropertyBarShowsLineStyle(tool)))
  {
    data->chrome().m_style_menu = AnnotationEditorStyleMenu::None;
    data->chrome().m_style_menu_rect = {};
  }
  fillSizeCombo(data);
  if (annotationEditorStrokePopupClosesOnTool(tool))
  {
    hideStrokePopup(data);
  }
  if (!annotationEditorPropertyBarShowsColor(tool))
  {
    hideColorPicker(data, false);
  }
  if (data->chrome().m_font_combo != nullptr)
  {
    // 全屏 UpdateLayeredWindow 会盖住 WS_POPUP ComboBox；字号改由 overlay 绘制/命中。
    ShowWindow(data->chrome().m_font_combo, SW_HIDE);
  }
  layoutEditorChrome(data->window().m_overlay, data);
  invalidateToolbar(data);
}

int hitTestColorSwatch(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsColor(data->core().m_controller.tool()))
  {
    return -1;
  }

  const POINT pt{x, y};
  for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
  {
    if (PtInRect(&data->chrome().m_color_swatch_rects[static_cast<std::size_t>(i)], pt) !=
        FALSE)
    {
      return i;
    }
  }
  return -1;
}

bool hitTestCurrentColorSwatch(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsColor(data->core().m_controller.tool()))
  {
    return false;
  }
  const POINT pt{x, y};
  return PtInRect(&data->chrome().m_current_color_rect, pt) != FALSE;
}

bool hitTestSizeCombo(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarPaintsSizeCombo(data->core().m_controller.tool()))
  {
    return false;
  }

  const POINT pt{x, y};
  return PtInRect(&data->chrome().m_size_combo_rect, pt) != FALSE;
}

void refreshInlineEditFont(AnnotationEditorHost* data)
{
  if (data == nullptr || data->inlineText().m_inline_edit == nullptr)
  {
    return;
  }
  const AnnotationStyle& style = data->core().m_controller.style();
  data->inlineText().m_inline_edit_font =
      createAnnotationTextFont(style, style.font_size);
  if (data->inlineText().m_inline_edit_font)
  {
    SendMessageW(data->inlineText().m_inline_edit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(data->inlineText().m_inline_edit_font.get()), TRUE);
  }
  applyInlineEditVisual(data);
  SetFocus(data->inlineText().m_inline_edit);
}

void applyPickedSizeValue(AnnotationEditorHost* data, bool mosaic, int value)
{
  if (data == nullptr)
  {
    return;
  }
  if (mosaic)
  {
    data->core().m_controller.setMosaicBlockSize(value);
    if (data->core().m_controller.isDrawing())
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

bool handleSizeComboWheel(AnnotationEditorHost* data, int delta)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarPaintsSizeCombo(data->core().m_controller.tool()))
  {
    return false;
  }

  const bool mosaic =
      annotationEditorPropertyBarShowsMosaicSize(data->core().m_controller.tool());
  const int current = mosaic ? data->core().m_controller.mosaicBlockSize()
                             : data->core().m_controller.style().font_size;
  const int steps = annotationEditorWheelDeltaToSteps(delta);
  const int next = mosaic ? annotationEditorStepMosaicBlockSize(current, steps)
                          : annotationEditorStepFontSize(current, steps);
  applyPickedSizeValue(data, mosaic, next);
  return true;
}

void pickSizeFromOverlayMenu(AnnotationEditorHost* data)
{
  if (data == nullptr || data->window().m_overlay == nullptr)
  {
    return;
  }

  const bool mosaic =
      annotationEditorPropertyBarShowsMosaicSize(data->core().m_controller.tool());
  const int* options = mosaic ? AnnotationEditorMosaicSizeOptions
                              : AnnotationEditorFontSizeOptions;
  const int count = mosaic ? AnnotationEditorMosaicSizeOptionCount
                           : AnnotationEditorFontSizeOptionCount;
  const int current = mosaic ? data->core().m_controller.mosaicBlockSize()
                             : data->core().m_controller.style().font_size;
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

  POINT origin{data->chrome().m_size_combo_rect.left, data->chrome().m_size_combo_rect.bottom};
  ClientToScreen(data->window().m_overlay, &origin);
  (void)SetForegroundWindow(data->window().m_overlay);
  const UINT cmd = TrackPopupMenu(
      menu,
      TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY |
          TPM_RETURNCMD,
      origin.x, origin.y, 0, data->window().m_overlay, nullptr);
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

bool hitTestStrokeChip(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsStroke(data->core().m_controller.tool()))
  {
    return false;
  }

  const POINT pt{x, y};
  return PtInRect(&data->chrome().m_stroke_chip_rect, pt) != FALSE;
}

int hitTestShapeToggle(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsShapeToggle(data->core().m_controller.tool()))
  {
    return -1;
  }

  const POINT pt{x, y};
  for (int i = 0; i < kGeometryShapeCount; ++i)
  {
    if (PtInRect(&data->chrome().m_shape_rects[static_cast<std::size_t>(i)], pt) != FALSE)
    {
      return i;
    }
  }
  return -1;
}

bool hitTestFill(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsFill(data->core().m_controller.tool()))
  {
    return false;
  }
  const POINT pt{x, y};
  return PtInRect(&data->chrome().m_fill_rect, pt) != FALSE;
}

bool hitTestBold(const AnnotationEditorHost* data, int x, int y)
{
  return data != nullptr &&
         annotationEditorPropertyBarShowsTextStyle(
             data->core().m_controller.tool()) &&
         PtInRect(&data->chrome().m_bold_rect, POINT{x, y}) != FALSE;
}

bool hitTestItalic(const AnnotationEditorHost* data, int x, int y)
{
  return data != nullptr &&
         annotationEditorPropertyBarShowsTextStyle(
             data->core().m_controller.tool()) &&
         PtInRect(&data->chrome().m_italic_rect, POINT{x, y}) != FALSE;
}

bool hitTestFontFaceChip(const AnnotationEditorHost* data, int x, int y)
{
  return data != nullptr &&
         annotationEditorPropertyBarShowsFontFace(
             data->core().m_controller.tool()) &&
         PtInRect(&data->chrome().m_font_face_rect, POINT{x, y}) != FALSE;
}

int hitTestFontMenu(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr || !data->chrome().m_font_menu_open)
  {
    return -1;
  }
  const RECT& rect = data->chrome().m_font_menu_rect;
  const std::vector<std::wstring>& fonts =
      data->chrome().m_font_catalog.cachedFonts();
  return annotationEditorFontMenuHitTest(
      AnnotationEditorRect{rect.left, rect.top, rect.right, rect.bottom},
      static_cast<int>(fonts.size()),
      data->chrome().m_font_menu_scroll_offset, x, y);
}

int findFontFaceIndex(const std::vector<std::wstring>& fonts,
                      const std::wstring& font_face)
{
  for (std::size_t i = 0; i < fonts.size(); ++i)
  {
    if (CompareStringOrdinal(fonts.at(i).c_str(), -1, font_face.c_str(), -1,
                             TRUE) == CSTR_EQUAL)
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void applyFontFace(AnnotationEditorHost* data, const std::wstring& font_face)
{
  if (data == nullptr)
  {
    return;
  }
  data->core().m_controller.setFontFace(font_face);
  refreshInlineEditFont(data);
  applyLiveTextStyle(data);
  invalidateToolbar(data);
}

void closeFontMenu(AnnotationEditorHost* data)
{
  if (data == nullptr || !data->chrome().m_font_menu_open)
  {
    return;
  }
  data->chrome().m_font_menu_open = false;
  data->chrome().m_font_menu_rect = {};
  invalidateToolbar(data);
}

bool handleFontWheel(AnnotationEditorHost* data, int x, int y, int delta)
{
  if (data == nullptr)
  {
    return false;
  }
  const int steps = annotationEditorWheelDeltaToSteps(delta);
  const POINT point{x, y};
  if (data->chrome().m_font_menu_open &&
      PtInRect(&data->chrome().m_font_menu_rect, point) != FALSE)
  {
    const int count = static_cast<int>(
        data->chrome().m_font_catalog.cachedFonts().size());
    data->chrome().m_font_menu_scroll_offset =
        annotationEditorFontMenuScrollOffset(
            data->chrome().m_font_menu_scroll_offset, count, -steps);
    invalidateToolbar(data);
    return true;
  }
  if (!hitTestFontFaceChip(data, x, y))
  {
    return false;
  }

  const std::vector<std::wstring>& fonts =
      data->chrome().m_font_catalog.fonts();
  const int current = findFontFaceIndex(
      fonts, data->core().m_controller.style().font_face);
  const int next = annotationEditorFontFaceStepIndex(
      current, static_cast<int>(fonts.size()), steps);
  if (next >= 0)
  {
    applyFontFace(data, fonts.at(static_cast<std::size_t>(next)));
  }
  return true;
}

int hitTestLineStyle(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      data->chrome().m_style_menu != AnnotationEditorStyleMenu::Line)
  {
    return -1;
  }
  const RECT& rect = data->chrome().m_style_menu_rect;
  return annotationEditorStyleMenuHitTest(
      AnnotationEditorRect{rect.left, rect.top, rect.right, rect.bottom},
      AnnotationLineStyleCount, x, y);
}

int hitTestArrowStyle(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      data->chrome().m_style_menu != AnnotationEditorStyleMenu::Arrow)
  {
    return -1;
  }
  const RECT& rect = data->chrome().m_style_menu_rect;
  return annotationEditorStyleMenuHitTest(
      AnnotationEditorRect{rect.left, rect.top, rect.right, rect.bottom},
      AnnotationArrowStyleCount, x, y);
}

bool hitTestLineStyleChip(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsLineStyle(data->core().m_controller.tool()))
  {
    return false;
  }
  return PtInRect(&data->chrome().m_line_style_chip_rect, POINT{x, y}) != FALSE;
}

bool hitTestArrowStyleChip(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr || !annotationEditorPropertyBarShowsArrowStyle(
                             data->core().m_controller.tool()))
  {
    return false;
  }
  return PtInRect(&data->chrome().m_arrow_style_chip_rect, POINT{x, y}) != FALSE;
}

void closeStyleMenu(AnnotationEditorHost* data)
{
  if (data == nullptr ||
      data->chrome().m_style_menu == AnnotationEditorStyleMenu::None)
  {
    return;
  }
  data->chrome().m_style_menu = AnnotationEditorStyleMenu::None;
  data->chrome().m_style_menu_rect = {};
  bindPropertyBarTooltips(data);
  invalidateToolbar(data);
}

bool handleStyleChipWheel(AnnotationEditorHost* data, int x, int y, int delta)
{
  if (data == nullptr)
  {
    return false;
  }
  const int steps = annotationEditorWheelDeltaToSteps(delta);
  if (hitTestArrowStyleChip(data, x, y))
  {
    data->core().m_controller.setArrowStyle(annotationArrowStyleStep(
        data->core().m_controller.style().arrow_style, steps));
  }
  else if (hitTestLineStyleChip(data, x, y))
  {
    data->core().m_controller.setLineStyle(annotationLineStyleStep(
        data->core().m_controller.style().line_style, steps));
  }
  else
  {
    return false;
  }
  if (data->core().m_controller.isDrawing())
  {
    invalidateImageArea(data);
  }
  invalidateToolbar(data);
  return true;
}

void handlePropertyBarClick(AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr)
  {
    return;
  }

  const int font_index = hitTestFontMenu(data, x, y);
  if (font_index >= 0)
  {
    const std::vector<std::wstring>& fonts =
        data->chrome().m_font_catalog.cachedFonts();
    applyFontFace(data, fonts.at(static_cast<std::size_t>(font_index)));
    closeFontMenu(data);
    return;
  }
  if (data->chrome().m_font_menu_open &&
      hitTestFontFaceChip(data, x, y))
  {
    closeFontMenu(data);
    return;
  }
  if (data->chrome().m_font_menu_open)
  {
    const POINT point{x, y};
    if (PtInRect(&data->chrome().m_font_menu_rect, point) != FALSE)
    {
      return;
    }
    closeFontMenu(data);
  }

  const int arrow_index = hitTestArrowStyle(data, x, y);
  if (arrow_index >= 0)
  {
    data->core().m_controller.setArrowStyle(
        AnnotationArrowStyleOptions[static_cast<std::size_t>(arrow_index)]);
    closeStyleMenu(data);
    invalidateImageArea(data);
    return;
  }
  const int line_index = hitTestLineStyle(data, x, y);
  if (line_index >= 0)
  {
    data->core().m_controller.setLineStyle(
        AnnotationLineStyleOptions[static_cast<std::size_t>(line_index)]);
    closeStyleMenu(data);
    invalidateImageArea(data);
    return;
  }
  if (data->chrome().m_style_menu == AnnotationEditorStyleMenu::Arrow &&
      hitTestArrowStyleChip(data, x, y))
  {
    closeStyleMenu(data);
    return;
  }
  if (data->chrome().m_style_menu == AnnotationEditorStyleMenu::Line &&
      hitTestLineStyleChip(data, x, y))
  {
    closeStyleMenu(data);
    return;
  }
  if (data->chrome().m_style_menu != AnnotationEditorStyleMenu::None)
  {
    const POINT point{x, y};
    if (PtInRect(&data->chrome().m_style_menu_rect, point) != FALSE)
    {
      return;
    }
    closeStyleMenu(data);
  }

  if (hitTestBold(data, x, y))
  {
    data->core().m_controller.setBold(
        !data->core().m_controller.style().bold);
    refreshInlineEditFont(data);
    applyLiveTextStyle(data);
    invalidateToolbar(data);
    return;
  }

  if (hitTestItalic(data, x, y))
  {
    data->core().m_controller.setItalic(
        !data->core().m_controller.style().italic);
    refreshInlineEditFont(data);
    applyLiveTextStyle(data);
    invalidateToolbar(data);
    return;
  }

  if (hitTestFontFaceChip(data, x, y))
  {
    closeStyleMenu(data);
    const std::vector<std::wstring>& fonts =
        data->chrome().m_font_catalog.fonts();
    const int selected = findFontFaceIndex(
        fonts, data->core().m_controller.style().font_face);
    data->chrome().m_font_menu_scroll_offset =
        annotationEditorFontMenuEnsureVisible(
            data->chrome().m_font_menu_scroll_offset, selected,
            static_cast<int>(fonts.size()));
    data->chrome().m_font_menu_open = true;
    layoutPropertyBar(data->window().m_overlay, data);
    invalidateToolbar(data);
    return;
  }

  const int shape_index = hitTestShapeToggle(data, x, y);
  if (shape_index >= 0)
  {
    data->core().m_controller.setTool(shape_index == 0 ? AnnotationTool::Rectangle
                                              : AnnotationTool::Ellipse);
    syncGeometryButton(data);
    resizeEditorChrome(data);
    return;
  }

  if (hitTestFill(data, x, y))
  {
    data->core().m_controller.setFilled(!data->core().m_controller.style().filled);
    if (data->core().m_controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    invalidateToolbar(data);
    return;
  }

  if (hitTestArrowStyleChip(data, x, y))
  {
    data->chrome().m_style_menu =
        data->chrome().m_style_menu == AnnotationEditorStyleMenu::Arrow
            ? AnnotationEditorStyleMenu::None
            : AnnotationEditorStyleMenu::Arrow;
    layoutPropertyBar(data->window().m_overlay, data);
    invalidateToolbar(data);
    return;
  }

  if (hitTestLineStyleChip(data, x, y))
  {
    data->chrome().m_style_menu =
        data->chrome().m_style_menu == AnnotationEditorStyleMenu::Line
            ? AnnotationEditorStyleMenu::None
            : AnnotationEditorStyleMenu::Line;
    layoutPropertyBar(data->window().m_overlay, data);
    invalidateToolbar(data);
    return;
  }

  const int color_index = hitTestColorSwatch(data, x, y);
  if (color_index >= 0)
  {
    hideColorPicker(data, false);
    data->core().m_controller.setColor(
        AnnotationStylePresetColors[static_cast<std::size_t>(color_index)]);
    applyLiveTextStyle(data);
    if (data->inlineText().m_inline_edit != nullptr)
    {
      SetFocus(data->inlineText().m_inline_edit);
    }
    if (data->core().m_controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    invalidateToolbar(data);
    return;
  }

  if (hitTestCurrentColorSwatch(data, x, y))
  {
    if (colorPickerIsVisible(data))
    {
      hideColorPicker(data, false);
    }
    else
    {
      showColorPicker(data);
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
    if (strokePopupIsVisible(data))
    {
      hideStrokePopup(data);
    }
    else
    {
      showStrokePopup(data);
    }
  }
}

bool pointerHitsStyleChrome(const AnnotationEditorHost* data)
{
  if (data == nullptr || data->window().m_overlay == nullptr)
  {
    return false;
  }

  POINT pt{};
  if (GetCursorPos(&pt) == FALSE)
  {
    return false;
  }
  ScreenToClient(data->window().m_overlay, &pt);
  const bool font_menu_hit =
      data->chrome().m_font_menu_open &&
      PtInRect(&data->chrome().m_font_menu_rect, pt) != FALSE;
  return hitTestEditorToolbar(data, pt.x, pt.y) >= 0 ||
         hitTestChromeBar(data, pt.x, pt.y) ||
         hitTestCurrentColorSwatch(data, pt.x, pt.y) ||
         hitTestColorSwatch(data, pt.x, pt.y) >= 0 ||
         hitTestSizeCombo(data, pt.x, pt.y) ||
         hitTestStrokeChip(data, pt.x, pt.y) ||
         hitTestShapeToggle(data, pt.x, pt.y) >= 0 ||
         hitTestFill(data, pt.x, pt.y) ||
         hitTestBold(data, pt.x, pt.y) ||
         hitTestItalic(data, pt.x, pt.y) ||
         hitTestFontFaceChip(data, pt.x, pt.y) ||
         font_menu_hit ||
         hitTestLineStyleChip(data, pt.x, pt.y) ||
         hitTestArrowStyleChip(data, pt.x, pt.y) ||
         hitTestLineStyle(data, pt.x, pt.y) >= 0 ||
         hitTestArrowStyle(data, pt.x, pt.y) >= 0;
}

void drawLineStyleSample(HDC hdc, const RECT& rect, AnnotationLineStyle style,
                         COLORREF color)
{
  const int left = rect.left + 10;
  const int right = rect.right - 22;
  const int y = (rect.top + rect.bottom) / 2;
  const GdiObject pen(CreatePen(PS_SOLID, 2, color));
  if (!pen || right <= left)
  {
    return;
  }
  const SelectGuard selected_pen(hdc, pen.get());
  struct LineStyleSamplePattern
  {
    int lengths[6]{};
    int count{0};
  };
  constexpr LineStyleSamplePattern Patterns[AnnotationLineStyleCount] = {
      {{100, 0, 0, 0, 0, 0}, 1}, {{10, 6, 0, 0, 0, 0}, 2},
      {{2, 5, 0, 0, 0, 0}, 2},   {{11, 5, 2, 5, 0, 0}, 4},
      {{10, 4, 2, 4, 2, 4}, 6}};
  const int index = annotationStyleOptionIndex(
      AnnotationLineStyleOptions, AnnotationLineStyleCount, style);
  const int safe_index = index >= 0 ? index : 0;
  const LineStyleSamplePattern& pattern = Patterns[safe_index];
  int x = left;
  bool drawing = true;
  int part = 0;
  while (x < right)
  {
    const int length = pattern.lengths[part];
    const int next = (std::min)(right, x + length);
    if (drawing)
    {
      MoveToEx(hdc, x, y, nullptr);
      LineTo(hdc, next, y);
    }
    drawing = !drawing;
    x = next;
    part = (part + 1) % pattern.count;
  }
}

void drawArrowStyleSample(HDC hdc, const RECT& rect,
                          AnnotationArrowStyle style, COLORREF color)
{
  const int left = rect.left + 10;
  const int right = rect.right - 22;
  const int y = (rect.top + rect.bottom) / 2;
  const GdiObject pen(CreatePen(PS_SOLID, 2, color));
  const GdiObject brush(CreateSolidBrush(color));
  if (!pen || !brush || right <= left)
  {
    return;
  }
  const SelectGuard selected_pen(hdc, pen.get());
  const SelectGuard selected_brush(hdc, brush.get());
  MoveToEx(hdc, left, y, nullptr);
  LineTo(hdc, right, y);

  const bool start = style == AnnotationArrowStyle::StartOpen ||
                     style == AnnotationArrowStyle::BothOpen ||
                     style == AnnotationArrowStyle::StartFilled ||
                     style == AnnotationArrowStyle::BothFilled ||
                     style == AnnotationArrowStyle::StartBar ||
                     style == AnnotationArrowStyle::BothBars;
  const bool end = style == AnnotationArrowStyle::EndOpen ||
                   style == AnnotationArrowStyle::BothOpen ||
                   style == AnnotationArrowStyle::EndFilled ||
                   style == AnnotationArrowStyle::BothFilled ||
                   style == AnnotationArrowStyle::EndBar ||
                   style == AnnotationArrowStyle::BothBars;
  const bool filled = style == AnnotationArrowStyle::EndFilled ||
                      style == AnnotationArrowStyle::StartFilled ||
                      style == AnnotationArrowStyle::BothFilled;
  const bool bar = style == AnnotationArrowStyle::EndBar ||
                   style == AnnotationArrowStyle::StartBar ||
                   style == AnnotationArrowStyle::BothBars;
  const auto draw_marker = [&](int tip, int direction)
  {
    if (bar)
    {
      MoveToEx(hdc, tip, y - 5, nullptr);
      LineTo(hdc, tip, y + 6);
      return;
    }
    const int base = tip - direction * 8;
    if (filled)
    {
      POINT triangle[3] = {{tip, y}, {base, y - 5}, {base, y + 5}};
      Polygon(hdc, triangle, 3);
      return;
    }
    MoveToEx(hdc, tip, y, nullptr);
    LineTo(hdc, base, y - 5);
    MoveToEx(hdc, tip, y, nullptr);
    LineTo(hdc, base, y + 5);
  };
  if (start)
  {
    draw_marker(left, -1);
  }
  if (end)
  {
    draw_marker(right, 1);
  }
}

void drawStyleChevron(HDC hdc, const RECT& rect, COLORREF color)
{
  const GdiObject pen(CreatePen(PS_SOLID, 1, color));
  if (!pen)
  {
    return;
  }
  const SelectGuard selected_pen(hdc, pen.get());
  const int x = rect.right - 12;
  const int y = (rect.top + rect.bottom) / 2;
  MoveToEx(hdc, x - 3, y - 1, nullptr);
  LineTo(hdc, x, y + 2);
  LineTo(hdc, x + 4, y - 2);
}

void paintStyleMenu(HDC hdc, const AnnotationEditorPaintSnapshot& snapshot)
{
  if (snapshot.style_menu == AnnotationEditorStyleMenu::None)
  {
    return;
  }
  drawToolbarBar(hdc, snapshot.style_menu_rect);
  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const int count = snapshot.style_menu == AnnotationEditorStyleMenu::Arrow
                        ? AnnotationArrowStyleCount
                        : AnnotationLineStyleCount;
  const AnnotationEditorRect menu{snapshot.style_menu_rect.left,
                                   snapshot.style_menu_rect.top,
                                   snapshot.style_menu_rect.right,
                                   snapshot.style_menu_rect.bottom};
  for (int i = 0; i < count; ++i)
  {
    const RECT item = toWinRect(annotationEditorStyleMenuItemRect(menu, i));
    const bool selected =
        snapshot.style_menu == AnnotationEditorStyleMenu::Arrow
            ? snapshot.style.arrow_style == AnnotationArrowStyleOptions[i]
            : snapshot.style.line_style == AnnotationLineStyleOptions[i];
    if (selected)
    {
      fillRoundRect(hdc, item, colors.selected_fill, colors.selected_fill,
                    DefaultModernToolbarMetrics.hover_radius);
    }
    if (snapshot.style_menu == AnnotationEditorStyleMenu::Arrow)
    {
      drawArrowStyleSample(hdc, item, AnnotationArrowStyleOptions[i],
                           colors.icon);
    }
    else
    {
      drawLineStyleSample(hdc, item, AnnotationLineStyleOptions[i],
                          colors.icon);
    }
  }
}

void drawTextStyleButton(HDC hdc, const RECT& rect, const wchar_t* label,
                         bool selected, bool hovered, HFONT font)
{
  const ModernToolbarColors colors = DefaultModernToolbarColors;
  if (selected || hovered)
  {
    const COLORREF fill = selected ? colors.selected_fill : colors.hover_fill;
    fillRoundRect(hdc, rect, fill, fill,
                  DefaultModernToolbarMetrics.hover_radius);
  }
  const HGDIOBJ old_font =
      font != nullptr ? SelectObject(hdc, font) : nullptr;
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, colors.icon);
  RECT text_rect = rect;
  DrawTextW(hdc, label, -1, &text_rect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (old_font != nullptr)
  {
    SelectObject(hdc, old_font);
  }
}

void paintFontMenu(HDC hdc, const AnnotationEditorPaintSnapshot& snapshot)
{
  if (!snapshot.font_menu_open ||
      snapshot.font_menu_rect.right <= snapshot.font_menu_rect.left)
  {
    return;
  }
  drawToolbarBar(hdc, snapshot.font_menu_rect);
  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const AnnotationEditorRect menu{snapshot.font_menu_rect.left,
                                   snapshot.font_menu_rect.top,
                                   snapshot.font_menu_rect.right,
                                   snapshot.font_menu_rect.bottom};
  const HGDIOBJ old_font = snapshot.combo_font != nullptr
                               ? SelectObject(hdc, snapshot.combo_font)
                               : nullptr;
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, colors.label);
  for (std::size_t i = 0; i < snapshot.visible_font_faces.size(); ++i)
  {
    const RECT item = toWinRect(annotationEditorFontMenuItemRect(
        menu, static_cast<int>(i)));
    const std::wstring& face = snapshot.visible_font_faces.at(i);
    const bool selected = CompareStringOrdinal(
                              face.c_str(), -1,
                              snapshot.style.font_face.c_str(), -1, TRUE) ==
                          CSTR_EQUAL;
    if (selected)
    {
      fillRoundRect(hdc, item, colors.selected_fill, colors.selected_fill,
                    DefaultModernToolbarMetrics.hover_radius);
    }
    RECT label = item;
    label.left += AnnotationEditorBarPadding;
    label.right -= AnnotationEditorBarPadding;
    DrawTextW(hdc, face.c_str(), static_cast<int>(face.size()), &label,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
  }
  if (old_font != nullptr)
  {
    SelectObject(hdc, old_font);
  }
}

void paintPropertyBar(HDC hdc,
                      const AnnotationEditorPaintSnapshot& snapshot,
                      bool draw_shell)
{
  if (hdc == nullptr || !annotationEditorShowsPropertyBar(snapshot.tool))
  {
    return;
  }

  if (snapshot.property_bar_rect.right <= snapshot.property_bar_rect.left)
  {
    return;
  }

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  if (draw_shell)
  {
    drawToolbarBar(hdc, snapshot.property_bar_rect);
  }

  const AnnotationTool tool = snapshot.tool;
  const AnnotationStyle& style = snapshot.style;

  if (annotationEditorPropertyBarShowsTextStyle(tool))
  {
    drawTextStyleButton(hdc, snapshot.bold_rect, L"B", style.bold,
                        snapshot.bold_hover, snapshot.combo_font);
    drawTextStyleButton(hdc, snapshot.italic_rect, L"I", style.italic,
                        snapshot.italic_hover, snapshot.combo_font);

    const RECT& font_chip = snapshot.font_face_rect;
    if (snapshot.font_face_hover || snapshot.font_menu_open)
    {
      const COLORREF fill = snapshot.font_menu_open
                                ? colors.selected_fill
                                : colors.hover_fill;
      fillRoundRect(hdc, font_chip, fill, fill,
                    DefaultModernToolbarMetrics.hover_radius);
    }
    const HGDIOBJ old_font = snapshot.combo_font != nullptr
                                 ? SelectObject(hdc, snapshot.combo_font)
                                 : nullptr;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, colors.label);
    RECT font_label = font_chip;
    font_label.left += AnnotationEditorBarPadding;
    font_label.right -= AnnotationEditorButtonWidth;
    DrawTextW(hdc, style.font_face.c_str(),
              static_cast<int>(style.font_face.size()), &font_label,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
                  DT_NOPREFIX);
    if (old_font != nullptr)
    {
      SelectObject(hdc, old_font);
    }
    drawStyleChevron(hdc, font_chip, colors.icon);
  }

  if (annotationEditorPropertyBarShowsShapeToggle(tool))
  {
    drawToolbarItem(hdc, snapshot.shape_rects[0], ToolbarIconKind::Rectangle,
                    false, tool == AnnotationTool::Rectangle, true, false);
    drawToolbarItem(hdc, snapshot.shape_rects[1], ToolbarIconKind::Ellipse, false,
                    tool == AnnotationTool::Ellipse, true, false);
  }
  if (annotationEditorPropertyBarShowsFill(tool))
  {
    drawToolbarItem(hdc, snapshot.fill_rect, ToolbarIconKind::Fill, false,
                    style.filled, true, false);
  }
  if (annotationEditorPropertyBarShowsLineStyle(tool))
  {
    const RECT& chip = snapshot.line_style_chip_rect;
    if (snapshot.line_style_chip_hover ||
        snapshot.style_menu == AnnotationEditorStyleMenu::Line)
    {
      fillRoundRect(hdc, chip, colors.selected_fill, colors.selected_fill,
                    DefaultModernToolbarMetrics.hover_radius);
    }
    drawLineStyleSample(hdc, chip, style.line_style, colors.icon);
    drawStyleChevron(hdc, chip, colors.icon);
  }
  if (annotationEditorPropertyBarShowsArrowStyle(tool))
  {
    const RECT& chip = snapshot.arrow_style_chip_rect;
    if (snapshot.arrow_style_chip_hover ||
        snapshot.style_menu == AnnotationEditorStyleMenu::Arrow)
    {
      fillRoundRect(hdc, chip, colors.selected_fill, colors.selected_fill,
                    DefaultModernToolbarMetrics.hover_radius);
    }
    drawArrowStyleSample(hdc, chip, style.arrow_style, colors.icon);
    drawStyleChevron(hdc, chip, colors.icon);
  }

  if (annotationEditorPropertyBarShowsColor(tool))
  {
    const RECT& current = snapshot.current_color_rect;
    if (current.right > current.left)
    {
      const bool custom = !colorMatchesAnyPresetRgb(style.color);
      const bool picker_open = snapshot.color_picker_visible;
      fillRoundRect(hdc, current, colorBgraToRef(style.color),
                    picker_open ? kSwatchSelectedBorderColor : kSwatchBorderColor,
                    kSwatchCornerRadius);
      if (custom)
      {
        constexpr int kRainbowHueCount = 3;
        constexpr int kRainbowHues[kRainbowHueCount] = {0, 120, 240};
        for (int i = 0; i < kRainbowHueCount; ++i)
        {
          RECT ring = current;
          InflateRect(&ring, i + 1, i + 1);
          const ColorBgra hue_color =
              hsvToRgb(kRainbowHues[i], ColorPercentMax, ColorPercentMax,
                       static_cast<std::uint8_t>(ColorChannelMax));
          const GdiObject brush(CreateSolidBrush(colorBgraToRef(hue_color)));
          if (brush)
          {
            FrameRect(hdc, &ring, brush.asBrush());
          }
        }
      }
    }
    for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
    {
      const ColorBgra& preset =
          AnnotationStylePresetColors[static_cast<std::size_t>(i)];
      const RECT& cell = snapshot.color_swatch_rects[static_cast<std::size_t>(i)];
      const bool selected = colorsMatchRgb(style.color, preset);
      fillRoundRect(hdc, cell, colorBgraToRef(preset),
                    selected ? kSwatchSelectedBorderColor : kSwatchBorderColor,
                    kSwatchCornerRadius);
    }
  }

  if (annotationEditorPropertyBarPaintsSizeCombo(tool) &&
      snapshot.size_combo_rect.right > snapshot.size_combo_rect.left)
  {
    const RECT& chip = snapshot.size_combo_rect;
    fillRoundRect(hdc, chip, colors.selected_fill, kSwatchBorderColor,
                  DefaultModernToolbarMetrics.hover_radius);
    const bool mosaic =
        annotationEditorPropertyBarShowsMosaicSize(tool);
    const int value = mosaic ? snapshot.mosaic_block_size
                             : style.font_size;
    wchar_t value_text[8]{};
    (void)swprintf_s(value_text, L"%d", value);
    RECT text_rect = chip;
    const HGDIOBJ old_font =
        snapshot.combo_font ? SelectObject(hdc, snapshot.combo_font)
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
    paintFontMenu(hdc, snapshot);
    return;
  }

  const int divider_x =
      snapshot.stroke_chip_rect.left - AnnotationEditorDividerGap / 2;
  drawToolbarDivider(hdc, divider_x, snapshot.property_bar_rect.top + 8,
                     snapshot.property_bar_rect.bottom - 8);

  const RECT& chip = snapshot.stroke_chip_rect;
  const bool popup_open = snapshot.stroke_popup_visible;
  if (snapshot.stroke_chip_hover || popup_open)
  {
    fillRoundRect(hdc, chip,
                  snapshot.stroke_chip_hover ? colors.hover_fill
                                          : colors.selected_fill,
                  snapshot.stroke_chip_hover ? colors.hover_fill
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
      snapshot.combo_font ? SelectObject(hdc, snapshot.combo_font) : nullptr;
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, colors.label);
  DrawTextW(hdc, value_text, -1, &value,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  if (old_font != nullptr)
  {
    SelectObject(hdc, old_font);
  }

  paintStyleMenu(hdc, snapshot);
  paintFontMenu(hdc, snapshot);
}

void paintEditorToolbar(HDC hdc,
                        const AnnotationEditorPaintSnapshot& snapshot,
                        bool draw_shell)
{
  if (hdc == nullptr)
  {
    return;
  }

  if (draw_shell)
  {
    drawToolbarBar(hdc, snapshot.main_bar_rect);
  }

  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    const EditorToolbarItem& item = snapshot.toolbar_items[i];
    bool selected = false;
    switch (item.id)
    {
      case kButtonGeometryId:
        selected = annotationEditorIsGeometryTool(snapshot.tool);
        break;
      case kButtonArrowId:
        selected = snapshot.tool == AnnotationTool::Arrow;
        break;
      case kButtonPenId:
        selected = snapshot.tool == AnnotationTool::Pen;
        break;
      case kButtonMosaicId:
        selected = snapshot.tool == AnnotationTool::Mosaic;
        break;
      case kButtonTextId:
        selected = snapshot.tool == AnnotationTool::Text;
        break;
      default:
        break;
    }
    const bool enabled =
        item.id != kButtonUndoId || snapshot.document.canUndo();
    const bool redo_enabled =
        item.id != kButtonRedoId || snapshot.document.canRedo();
    const ToolbarItemModel toolbar_item{
        item.icon,
        i == snapshot.toolbar_hover,
        selected,
        enabled && redo_enabled,
        item.accent,
        item.id == kButtonGeometryId};
    drawToolbarItem(hdc, item.rect, toolbar_item);
  }

  const int divider_top = snapshot.main_bar_rect.top + 8;
  const int divider_bottom = snapshot.main_bar_rect.bottom - 8;
  for (int i = 0; i < AnnotationEditorDividerCount; ++i)
  {
    if (snapshot.toolbar_divider_x[i] > 0)
    {
      drawToolbarDivider(hdc, snapshot.toolbar_divider_x[i], divider_top,
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

bool createButtons(HWND hwnd, AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return false;
  }

  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_WIN95_CLASSES;
  (void)InitCommonControlsEx(&icc);

  data->chrome().m_font_combo = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"COMBOBOX", L"",
      WS_POPUP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0,
      AnnotationEditorFontComboWidth, AnnotationEditorFontComboDropHeight, hwnd,
      nullptr, GetModuleHandleW(nullptr), nullptr);
  if (data->chrome().m_font_combo == nullptr)
  {
    return false;
  }
  SetWindowLongPtrW(data->chrome().m_font_combo, GWLP_ID,
                    static_cast<LONG_PTR>(kFontComboId));

  data->chrome().m_combo_font.reset(CreateFontW(
      -kComboFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace));
  if (data->chrome().m_combo_font)
  {
    SendMessageW(data->chrome().m_font_combo, WM_SETFONT,
                 reinterpret_cast<WPARAM>(data->chrome().m_combo_font.get()), TRUE);
  }

  data->core().m_controller.setFontSize(DefaultFontSize);
  data->core().m_controller.setMosaicBlockSize(DefaultMosaicBlockSize);
  fillSizeCombo(data);

  resizeEditorChrome(data);

  data->chrome().m_tooltip = createToolbarTooltip(hwnd);
  bindEditorTooltips(data);
  return true;
}

void invalidateToolbar(AnnotationEditorHost* data)
{
  if (data == nullptr || data->window().m_overlay == nullptr)
  {
    return;
  }
  InvalidateRect(data->window().m_overlay, nullptr, FALSE);
}

bool hitTestChromeBar(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr)
  {
    return false;
  }
  const POINT pt{x, y};
  if (PtInRect(&data->chrome().m_main_bar_rect, pt) != FALSE)
  {
    return true;
  }
  if (PtInRect(&data->chrome().m_property_bar_rect, pt) != FALSE)
  {
    return true;
  }
  if (data->chrome().m_style_menu != AnnotationEditorStyleMenu::None &&
      PtInRect(&data->chrome().m_style_menu_rect, pt) != FALSE)
  {
    return true;
  }
  return data->chrome().m_font_menu_open &&
         PtInRect(&data->chrome().m_font_menu_rect, pt) != FALSE;
}

void bindEditorTooltips(AnnotationEditorHost* data)
{
  if (data == nullptr || data->window().m_overlay == nullptr || data->chrome().m_tooltip == nullptr)
  {
    return;
  }
  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    const wchar_t* tip =
        (data->chrome().m_toolbar_items[static_cast<std::size_t>(i)].id ==
         kButtonGeometryId)
            ? toolbarIconLabel(ToolbarIconKind::Geometry)
            : toolbarIconLabel(
                  data->chrome().m_toolbar_items[static_cast<std::size_t>(i)].icon);
    bindToolbarTooltip(data->chrome().m_tooltip, data->window().m_overlay,
                       data->chrome().m_toolbar_items[static_cast<std::size_t>(i)].id,
                       data->chrome().m_toolbar_items[static_cast<std::size_t>(i)].rect,
                       tip, data->chrome().m_tooltip_text[static_cast<std::size_t>(i)],
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

void beginChromeDrag(AnnotationEditorHost* data, HWND hwnd, int /*x*/, int /*y*/)
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
  data->chrome().m_chrome_dragging = true;
  data->chrome().m_chrome_drag_start_x = cursor.x;
  data->chrome().m_chrome_drag_start_y = cursor.y;
  data->chrome().m_chrome_drag_origin_x = data->chrome().m_chrome_offset_x;
  data->chrome().m_chrome_drag_origin_y = data->chrome().m_chrome_offset_y;
  closeStyleMenu(data);
  closeFontMenu(data);
  hideStrokePopup(data);
  hideColorPicker(data, false);
  SetCapture(hwnd);
}

void updateChromeDrag(AnnotationEditorHost* data, int /*x*/, int /*y*/)
{
  if (data == nullptr || !data->chrome().m_chrome_dragging || data->window().m_overlay == nullptr)
  {
    return;
  }
  POINT cursor{};
  if (GetCursorPos(&cursor) == FALSE)
  {
    return;
  }
  data->chrome().m_chrome_offset_x =
      data->chrome().m_chrome_drag_origin_x + (cursor.x - data->chrome().m_chrome_drag_start_x);
  data->chrome().m_chrome_offset_y =
      data->chrome().m_chrome_drag_origin_y + (cursor.y - data->chrome().m_chrome_drag_start_y);
  const RECT old_chrome =
      unionChromeRects(data->chrome().m_main_bar_rect, data->chrome().m_property_bar_rect);
  layoutEditorChrome(data->window().m_overlay, data);
  const RECT new_chrome =
      unionChromeRects(data->chrome().m_main_bar_rect, data->chrome().m_property_bar_rect);
  invalidateChromeMove(data->window().m_overlay, old_chrome, new_chrome);
}

void endChromeDrag(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  if (data->chrome().m_chrome_dragging)
  {
    data->chrome().m_chrome_dragging = false;
    ReleaseCapture();
  }
}

int hitTestEditorToolbar(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr)
  {
    return -1;
  }
  const POINT pt{x, y};
  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    if (PtInRect(&data->chrome().m_toolbar_items[i].rect, pt) != FALSE)
    {
      return i;
    }
  }
  return -1;
}

void handleToolbarItemClick(AnnotationEditorHost* data, UINT id)
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

void handleToolCommand(AnnotationEditorHost* data, UINT id)
{
  if (data == nullptr)
  {
    return;
  }

  hideStrokePopup(data);
  hideColorPicker(data, false);
  closeStyleMenu(data);
  closeFontMenu(data);
  if (id != kButtonTextId)
  {
    commitInlineText(data);
    clearTextSelection(data);
  }

  switch (id)
  {
    case kButtonGeometryId:
      data->core().m_controller.setTool(data->chrome().m_last_geometry_tool);
      break;
    case kButtonArrowId:
      data->core().m_controller.setTool(AnnotationTool::Arrow);
      break;
    case kButtonPenId:
      data->core().m_controller.setTool(AnnotationTool::Pen);
      break;
    case kButtonMosaicId:
      data->core().m_controller.setTool(AnnotationTool::Mosaic);
      break;
    case kButtonTextId:
      data->core().m_controller.setTool(AnnotationTool::Text);
      break;
    case kButtonUndoId:
      cancelInlineText(data);
      resetTextGesture(data);
      clearTextSelection(data);
      if (data->core().m_controller.undo(data->core().m_session.engine()))
      {
        invalidateImageArea(data);
      }
      break;
    case kButtonRedoId:
      cancelInlineText(data);
      resetTextGesture(data);
      clearTextSelection(data);
      if (data->core().m_session.engine().redo())
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
}  // namespace qingying
