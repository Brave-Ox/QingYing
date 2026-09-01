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

void fillSizeCombo(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_font_combo == nullptr)
  {
    return;
  }

  const bool mosaic =
      annotationEditorPropertyBarShowsMosaicSize(data->m_controller.tool());
  const int* options = mosaic ? AnnotationEditorMosaicSizeOptions
                              : AnnotationEditorFontSizeOptions;
  const int count = mosaic ? AnnotationEditorMosaicSizeOptionCount
                           : AnnotationEditorFontSizeOptionCount;
  const int current = mosaic ? data->m_controller.mosaicBlockSize()
                             : data->m_controller.style().font_size;

  data->m_size_combo_syncing = true;
  SendMessageW(data->m_font_combo, CB_RESETCONTENT, 0, 0);
  int selected = 0;
  for (int i = 0; i < count; ++i)
  {
    wchar_t label[16]{};
    swprintf_s(label, L"%d", options[i]);
    SendMessageW(data->m_font_combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(label));
    if (options[i] == current)
    {
      selected = i;
    }
  }
  SendMessageW(data->m_font_combo, CB_SETCURSEL, static_cast<WPARAM>(selected),
               0);
  data->m_size_combo_syncing = false;
}

void bindSizeComboTooltip(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_tooltip == nullptr || data->m_overlay == nullptr)
  {
    return;
  }

  RECT local = data->m_size_combo_rect;
  const bool show_combo =
      annotationEditorPropertyBarPaintsSizeCombo(data->m_controller.tool()) &&
      local.right > local.left;

  const wchar_t* tip = L"";
  if (show_combo)
  {
    tip = annotationEditorPropertyBarShowsMosaicSize(data->m_controller.tool())
              ? L"\x5757\x5927\x5C0F"
              : L"\x5B57\x53F7";
  }
  bindToolbarTooltip(data->m_tooltip, data->m_overlay, kFontComboId, local, tip,
                     data->m_tooltip_text[kComboTooltipSlot],
                     kToolbarTooltipMaxChars);
}

void bindPropertyBarTooltips(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_tooltip == nullptr || data->m_overlay == nullptr)
  {
    return;
  }

  const HWND overlay = data->m_overlay;
  const HWND tooltip = data->m_tooltip;
  bindToolbarTooltip(tooltip, overlay, kTipShapeRectId, data->m_shape_rects[0],
                     toolbarIconLabel(ToolbarIconKind::Rectangle),
                     data->m_tooltip_text[kShapeTooltipSlot],
                     kToolbarTooltipMaxChars);
  bindToolbarTooltip(tooltip, overlay, kTipShapeEllipseId, data->m_shape_rects[1],
                     toolbarIconLabel(ToolbarIconKind::Ellipse),
                     data->m_tooltip_text[kShapeTooltipSlot + 1],
                     kToolbarTooltipMaxChars);
  bindToolbarTooltip(tooltip, overlay, kTipFillId, data->m_fill_rect,
                     toolbarIconLabel(ToolbarIconKind::Fill),
                     data->m_tooltip_text[kFillTooltipSlot],
                     kToolbarTooltipMaxChars);

  const ToolbarIconKind line_icons[AnnotationLineStyleCount] = {
      ToolbarIconKind::LineSolid, ToolbarIconKind::LineDashed,
      ToolbarIconKind::LineDotted};
  for (int i = 0; i < AnnotationLineStyleCount; ++i)
  {
    bindToolbarTooltip(
        tooltip, overlay, kTipLineStyleBaseId + static_cast<UINT>(i),
        data->m_line_style_rects[static_cast<std::size_t>(i)],
        toolbarIconLabel(line_icons[static_cast<std::size_t>(i)]),
        data->m_tooltip_text[kLineStyleTooltipSlot + i],
        kToolbarTooltipMaxChars);
  }

  bindToolbarTooltip(tooltip, overlay, kTipStrokeId, data->m_stroke_chip_rect,
                     toolbarIconLabel(ToolbarIconKind::StrokeWidth),
                     data->m_tooltip_text[kStrokeTooltipSlot],
                     kToolbarTooltipMaxChars);

  for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
  {
    bindToolbarTooltip(tooltip, overlay, kTipColorBaseId + static_cast<UINT>(i),
                       data->m_color_swatch_rects[static_cast<std::size_t>(i)],
                       toolbarColorPresetLabel(i),
                       data->m_tooltip_text[kColorTooltipSlot + i],
                       kToolbarTooltipMaxChars);
  }

  bindSizeComboTooltip(data);
}

void syncSizeFromCombo(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_font_combo == nullptr ||
      data->m_size_combo_syncing)
  {
    return;
  }

  const LRESULT index = SendMessageW(data->m_font_combo, CB_GETCURSEL, 0, 0);
  if (index == CB_ERR || index < 0)
  {
    return;
  }

  if (annotationEditorPropertyBarShowsMosaicSize(data->m_controller.tool()))
  {
    if (index >= AnnotationEditorMosaicSizeOptionCount)
    {
      return;
    }
    data->m_controller.setMosaicBlockSize(
        AnnotationEditorMosaicSizeOptions[static_cast<std::size_t>(index)]);
    if (data->m_controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    return;
  }

  if (index >= AnnotationEditorFontSizeOptionCount)
  {
    return;
  }

  data->m_controller.setFontSize(
      AnnotationEditorFontSizeOptions[static_cast<std::size_t>(index)]);
  if (data->m_inline_edit != nullptr)
  {
    data->m_inline_edit_font.reset(CreateFontW(
        -data->m_controller.style().font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
        FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace));
    if (data->m_inline_edit_font)
    {
      SendMessageW(data->m_inline_edit, WM_SETFONT,
                   reinterpret_cast<WPARAM>(data->m_inline_edit_font.get()),
                   TRUE);
    }
    applyInlineEditVisual(data);
  }
  if (data->m_inline_edit != nullptr)
  {
    SetFocus(data->m_inline_edit);
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
  data->m_controller.setColor(annotation.style.color);
  selectFontSizeInCombo(data, annotation.style.font_size);
  invalidateToolbar(data);
}

void applyLiveTextStyle(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_overlay == nullptr)
  {
    return;
  }

  applyInlineEditVisual(data);

  const std::size_t index = annotationEditorTextStyleTargetIndex(
      data->m_selected_text_index, data->m_editing_text_index,
      data->m_session.engine().document().count());
  if (index == AnnotationEditorInvalidIndex)
  {
    return;
  }

  Annotation updated = data->m_session.engine().document().items().at(index);
  if (updated.type != AnnotationType::Text)
  {
    return;
  }

  updated.style.color = data->m_controller.style().color;
  updated.style.font_size = data->m_controller.style().font_size;
  fillTextHitBounds(data->m_overlay, updated);
  if (data->m_session.engine().replaceAt(index, updated))
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
    data->m_shape_rects[static_cast<std::size_t>(i)] = {};
  }
  data->m_fill_rect = {};
  for (int i = 0; i < AnnotationLineStyleCount; ++i)
  {
    data->m_line_style_rects[static_cast<std::size_t>(i)] = {};
  }
  data->m_stroke_chip_rect = {};
  data->m_size_combo_rect = {};
  for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
  {
    data->m_color_swatch_rects[static_cast<std::size_t>(i)] = {};
  }
}

void syncGeometryButton(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  if (annotationEditorIsGeometryTool(data->m_controller.tool()))
  {
    data->m_last_geometry_tool = data->m_controller.tool();
  }
  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    if (data->m_toolbar_items[static_cast<std::size_t>(i)].id != kButtonGeometryId)
    {
      continue;
    }
    data->m_toolbar_items[static_cast<std::size_t>(i)].icon =
        (data->m_last_geometry_tool == AnnotationTool::Ellipse)
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

  const Image& source = data->m_session.source();
  const int default_x = data->m_image_screen_x;
  const int default_y =
      data->m_image_screen_y + source.height + AnnotationEditorChromeImageGap;
  const int main_width = annotationEditorMainToolbarWidth();
  const int property_width =
      annotationEditorShowsPropertyBar(data->m_controller.tool())
          ? annotationEditorPropertyBarWidth()
          : 0;
  const int span_width =
      annotationEditorChromeSpanWidth(main_width, property_width);
  const int chrome_height =
      annotationEditorChromeHeight(data->m_controller.tool()) -
      AnnotationEditorChromeImageGap;
  const int screen_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int screen_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int screen_right = screen_left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int screen_bottom = screen_top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  annotationEditorClampChromeOffset(data->m_chrome_offset_x, data->m_chrome_offset_y,
                                    default_x, default_y, span_width,
                                    chrome_height, screen_left, screen_top,
                                    screen_right, screen_bottom);

  const int chrome_screen_x = default_x + data->m_chrome_offset_x;
  const int chrome_screen_y = default_y + data->m_chrome_offset_y;
  // 窗口已铺满虚拟屏，禁止再 SetWindowPos：拖栏时改窗口原点会让截图框先跟着走再被重绘拉回，边缘处明显抖动。
  const int bar_left = chrome_screen_x - screen_left;
  const int bar_top = chrome_screen_y - screen_top;
  const int bar_height = annotationEditorToolbarHeight();
  data->m_main_bar_rect = {bar_left, bar_top, bar_left + main_width,
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
    data->m_toolbar_items[static_cast<std::size_t>(item_index)].id = spec.id;
    data->m_toolbar_items[static_cast<std::size_t>(item_index)].icon = spec.icon;
    data->m_toolbar_items[static_cast<std::size_t>(item_index)].accent = spec.accent;
    data->m_toolbar_items[static_cast<std::size_t>(item_index)].rect = {
        x, y, x + AnnotationEditorButtonWidth, y + AnnotationEditorButtonHeight};
    x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
    ++item_index;
  }

  data->m_toolbar_divider_x[0] =
      x - AnnotationEditorButtonGap + AnnotationEditorDividerGap / 2;
  x += AnnotationEditorDividerGap - AnnotationEditorButtonGap;

  data->m_toolbar_items[static_cast<std::size_t>(item_index)] = {
      kButtonUndoId,
      ToolbarIconKind::Undo,
      false,
      {x, y, x + AnnotationEditorButtonWidth, y + AnnotationEditorButtonHeight}};
  ++item_index;
  x += AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  data->m_toolbar_divider_x[1] =
      x - AnnotationEditorButtonGap + AnnotationEditorDividerGap / 2;
  x += AnnotationEditorDividerGap - AnnotationEditorButtonGap;

  const int action_total =
      AnnotationEditorButtonWidth * 2 + AnnotationEditorButtonGap;
  const int confirm_x =
      (std::max)(x, bar_left + main_width - action_total -
                        AnnotationEditorBarPadding);

  data->m_toolbar_items[static_cast<std::size_t>(item_index)] = {
      kButtonConfirmId,
      ToolbarIconKind::Confirm,
      true,
      {confirm_x, y, confirm_x + AnnotationEditorButtonWidth,
       y + AnnotationEditorButtonHeight}};
  ++item_index;

  const int cancel_x =
      confirm_x + AnnotationEditorButtonWidth + AnnotationEditorButtonGap;
  data->m_toolbar_items[static_cast<std::size_t>(item_index)] = {
      kButtonCancelId,
      ToolbarIconKind::Cancel,
      false,
      {cancel_x, y, cancel_x + AnnotationEditorButtonWidth,
       y + AnnotationEditorButtonHeight}};

  layoutPropertyBar(hwnd, data);
  syncGeometryButton(data);
  bindEditorTooltips(data);
  if (data->m_inline_edit != nullptr)
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

  data->m_property_bar_rect = {};
  const AnnotationTool tool = data->m_controller.tool();
  resetPropertyBarRects(data);
  if (!annotationEditorShowsPropertyBar(tool))
  {
    if (data->m_font_combo != nullptr)
    {
      ShowWindow(data->m_font_combo, SW_HIDE);
    }
    return;
  }

  const int y = data->m_main_bar_rect.bottom + AnnotationEditorChromeStackGap +
                AnnotationEditorBarPadding;
  const int bar_left = data->m_main_bar_rect.left;
  const int bar_top = data->m_main_bar_rect.bottom + AnnotationEditorChromeStackGap;
  const int swatch = AnnotationEditorColorSwatchSize;
  const int swatch_y = y + (AnnotationEditorButtonHeight - swatch) / 2;
  int x = bar_left + AnnotationEditorBarPadding;
  const int mosaic_combo_x = x;

  if (annotationEditorPropertyBarShowsShapeToggle(tool))
  {
    data->m_shape_rects[0] = takeToolbarButtonRect(x, y);
    data->m_shape_rects[1] = takeToolbarButtonRect(x, y);
    skipToolbarDivider(x);
  }
  if (annotationEditorPropertyBarShowsFill(tool))
  {
    data->m_fill_rect = takeToolbarButtonRect(x, y);
    skipToolbarDivider(x);
  }
  if (annotationEditorPropertyBarShowsLineStyle(tool))
  {
    for (int i = 0; i < AnnotationLineStyleCount; ++i)
    {
      data->m_line_style_rects[static_cast<std::size_t>(i)] =
          takeToolbarButtonRect(x, y);
    }
    skipToolbarDivider(x);
  }

  if (annotationEditorPropertyBarShowsStroke(tool) &&
      annotationEditorIsGeometryTool(tool))
  {
    data->m_stroke_chip_rect =
        takeToolbarSizedRect(x, y, AnnotationEditorStrokeChipWidth);
    skipToolbarDivider(x);
  }

  int combo_x = x;
  if (annotationEditorPropertyBarShowsColor(tool))
  {
    for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
    {
      data->m_color_swatch_rects[static_cast<std::size_t>(i)] = {
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
    data->m_stroke_chip_rect =
        takeToolbarSizedRect(x, y, AnnotationEditorStrokeChipWidth);
  }

  data->m_size_combo_rect = {};
  if (annotationEditorPropertyBarShowsSizeCombo(tool))
  {
    const int placed_x =
        annotationEditorPropertyBarShowsMosaicSize(tool) ? mosaic_combo_x
                                                         : combo_x;
    data->m_size_combo_rect = {placed_x, y,
                             placed_x + AnnotationEditorFontComboWidth,
                             y + AnnotationEditorButtonHeight};
    x = (std::max)(x, placed_x + AnnotationEditorFontComboWidth +
                          AnnotationEditorButtonGap);
  }
  if (data->m_font_combo != nullptr)
  {
    ShowWindow(data->m_font_combo, SW_HIDE);
  }

  const int content_right =
      (std::max)(x - AnnotationEditorButtonGap,
                 bar_left + AnnotationEditorBarPadding);
  data->m_property_bar_rect = {
      bar_left, bar_top,
      content_right + AnnotationEditorBarPadding,
      bar_top + annotationEditorToolbarHeight()};

  bindPropertyBarTooltips(data);
}

void resizeEditorChrome(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_overlay == nullptr)
  {
    return;
  }

  const AnnotationTool tool = data->m_controller.tool();
  fillSizeCombo(data);
  if (annotationEditorStrokePopupClosesOnTool(tool))
  {
    hideStrokePopup(data);
  }
  if (data->m_font_combo != nullptr)
  {
    // 全屏 UpdateLayeredWindow 会盖住 WS_POPUP ComboBox；字号改由 overlay 绘制/命中。
    ShowWindow(data->m_font_combo, SW_HIDE);
  }
  layoutEditorChrome(data->m_overlay, data);
  invalidateToolbar(data);
}

int hitTestColorSwatch(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsColor(data->m_controller.tool()))
  {
    return -1;
  }

  const POINT pt{x, y};
  for (int i = 0; i < AnnotationStylePresetColorCount; ++i)
  {
    if (PtInRect(&data->m_color_swatch_rects[static_cast<std::size_t>(i)], pt) !=
        FALSE)
    {
      return i;
    }
  }
  return -1;
}

bool hitTestSizeCombo(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarPaintsSizeCombo(data->m_controller.tool()))
  {
    return false;
  }

  const POINT pt{x, y};
  return PtInRect(&data->m_size_combo_rect, pt) != FALSE;
}

void refreshInlineEditFont(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_inline_edit == nullptr)
  {
    return;
  }
  data->m_inline_edit_font.reset(CreateFontW(
      -data->m_controller.style().font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
      FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace));
  if (data->m_inline_edit_font)
  {
    SendMessageW(data->m_inline_edit, WM_SETFONT,
                 reinterpret_cast<WPARAM>(data->m_inline_edit_font.get()), TRUE);
  }
  applyInlineEditVisual(data);
  SetFocus(data->m_inline_edit);
}

void applyPickedSizeValue(AnnotationEditorHost* data, bool mosaic, int value)
{
  if (data == nullptr)
  {
    return;
  }
  if (mosaic)
  {
    data->m_controller.setMosaicBlockSize(value);
    if (data->m_controller.isDrawing())
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
      !annotationEditorPropertyBarPaintsSizeCombo(data->m_controller.tool()))
  {
    return false;
  }

  const bool mosaic =
      annotationEditorPropertyBarShowsMosaicSize(data->m_controller.tool());
  const int current = mosaic ? data->m_controller.mosaicBlockSize()
                             : data->m_controller.style().font_size;
  const int steps = annotationEditorWheelDeltaToSteps(delta);
  const int next = mosaic ? annotationEditorStepMosaicBlockSize(current, steps)
                          : annotationEditorStepFontSize(current, steps);
  applyPickedSizeValue(data, mosaic, next);
  return true;
}

void pickSizeFromOverlayMenu(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_overlay == nullptr)
  {
    return;
  }

  const bool mosaic =
      annotationEditorPropertyBarShowsMosaicSize(data->m_controller.tool());
  const int* options = mosaic ? AnnotationEditorMosaicSizeOptions
                              : AnnotationEditorFontSizeOptions;
  const int count = mosaic ? AnnotationEditorMosaicSizeOptionCount
                           : AnnotationEditorFontSizeOptionCount;
  const int current = mosaic ? data->m_controller.mosaicBlockSize()
                             : data->m_controller.style().font_size;
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

  POINT origin{data->m_size_combo_rect.left, data->m_size_combo_rect.bottom};
  ClientToScreen(data->m_overlay, &origin);
  (void)SetForegroundWindow(data->m_overlay);
  const UINT cmd = TrackPopupMenu(
      menu,
      TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY |
          TPM_RETURNCMD,
      origin.x, origin.y, 0, data->m_overlay, nullptr);
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
      !annotationEditorPropertyBarShowsStroke(data->m_controller.tool()))
  {
    return false;
  }

  const POINT pt{x, y};
  return PtInRect(&data->m_stroke_chip_rect, pt) != FALSE;
}

int hitTestShapeToggle(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsShapeToggle(data->m_controller.tool()))
  {
    return -1;
  }

  const POINT pt{x, y};
  for (int i = 0; i < kGeometryShapeCount; ++i)
  {
    if (PtInRect(&data->m_shape_rects[static_cast<std::size_t>(i)], pt) != FALSE)
    {
      return i;
    }
  }
  return -1;
}

bool hitTestFill(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsFill(data->m_controller.tool()))
  {
    return false;
  }
  const POINT pt{x, y};
  return PtInRect(&data->m_fill_rect, pt) != FALSE;
}

int hitTestLineStyle(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr ||
      !annotationEditorPropertyBarShowsLineStyle(data->m_controller.tool()))
  {
    return -1;
  }

  const POINT pt{x, y};
  for (int i = 0; i < AnnotationLineStyleCount; ++i)
  {
    if (PtInRect(&data->m_line_style_rects[static_cast<std::size_t>(i)], pt) !=
        FALSE)
    {
      return i;
    }
  }
  return -1;
}

void handlePropertyBarClick(AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr)
  {
    return;
  }

  const int shape_index = hitTestShapeToggle(data, x, y);
  if (shape_index >= 0)
  {
    data->m_controller.setTool(shape_index == 0 ? AnnotationTool::Rectangle
                                              : AnnotationTool::Ellipse);
    syncGeometryButton(data);
    resizeEditorChrome(data);
    return;
  }

  if (hitTestFill(data, x, y))
  {
    data->m_controller.setFilled(!data->m_controller.style().filled);
    if (data->m_controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    invalidateToolbar(data);
    return;
  }

  const int line_index = hitTestLineStyle(data, x, y);
  if (line_index >= 0)
  {
    data->m_controller.setLineStyle(
        AnnotationLineStyleOptions[static_cast<std::size_t>(line_index)]);
    if (data->m_controller.isDrawing())
    {
      invalidateImageArea(data);
    }
    invalidateToolbar(data);
    return;
  }

  const int color_index = hitTestColorSwatch(data, x, y);
  if (color_index >= 0)
  {
    data->m_controller.setColor(
        AnnotationStylePresetColors[static_cast<std::size_t>(color_index)]);
    applyLiveTextStyle(data);
    if (data->m_inline_edit != nullptr)
    {
      SetFocus(data->m_inline_edit);
    }
    if (data->m_controller.isDrawing())
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
  if (data == nullptr || data->m_overlay == nullptr)
  {
    return false;
  }

  POINT pt{};
  if (GetCursorPos(&pt) == FALSE)
  {
    return false;
  }
  ScreenToClient(data->m_overlay, &pt);
  return hitTestEditorToolbar(data, pt.x, pt.y) >= 0 ||
         hitTestChromeBar(data, pt.x, pt.y) ||
         hitTestColorSwatch(data, pt.x, pt.y) >= 0 ||
         hitTestSizeCombo(data, pt.x, pt.y) ||
         hitTestStrokeChip(data, pt.x, pt.y) ||
         hitTestShapeToggle(data, pt.x, pt.y) >= 0 ||
         hitTestFill(data, pt.x, pt.y) ||
         hitTestLineStyle(data, pt.x, pt.y) >= 0;
}

void paintPropertyBar(HDC hdc, AnnotationEditorHost* data, bool draw_shell)
{
  if (hdc == nullptr || data == nullptr ||
      !annotationEditorShowsPropertyBar(data->m_controller.tool()))
  {
    return;
  }

  if (data->m_property_bar_rect.right <= data->m_property_bar_rect.left)
  {
    return;
  }

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  if (draw_shell)
  {
    drawToolbarBar(hdc, data->m_property_bar_rect);
  }

  const AnnotationTool tool = data->m_controller.tool();
  const AnnotationStyle& style = data->m_controller.style();

  if (annotationEditorPropertyBarShowsShapeToggle(tool))
  {
    drawToolbarItem(hdc, data->m_shape_rects[0], ToolbarIconKind::Rectangle,
                    false, tool == AnnotationTool::Rectangle, true, false);
    drawToolbarItem(hdc, data->m_shape_rects[1], ToolbarIconKind::Ellipse, false,
                    tool == AnnotationTool::Ellipse, true, false);
  }
  if (annotationEditorPropertyBarShowsFill(tool))
  {
    drawToolbarItem(hdc, data->m_fill_rect, ToolbarIconKind::Fill, false,
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
      drawToolbarItem(hdc, data->m_line_style_rects[static_cast<std::size_t>(i)],
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
      const RECT& cell = data->m_color_swatch_rects[static_cast<std::size_t>(i)];
      const bool selected = colorsMatch(style.color, preset);
      fillRoundRect(hdc, cell, colorBgraToRef(preset),
                    selected ? kSwatchSelectedBorderColor : kSwatchBorderColor,
                    kSwatchCornerRadius);
    }
  }

  if (annotationEditorPropertyBarPaintsSizeCombo(tool) &&
      data->m_size_combo_rect.right > data->m_size_combo_rect.left)
  {
    const RECT& chip = data->m_size_combo_rect;
    fillRoundRect(hdc, chip, colors.selected_fill, kSwatchBorderColor,
                  DefaultModernToolbarMetrics.hover_radius);
    const bool mosaic =
        annotationEditorPropertyBarShowsMosaicSize(tool);
    const int value = mosaic ? data->m_controller.mosaicBlockSize()
                             : style.font_size;
    wchar_t value_text[8]{};
    (void)swprintf_s(value_text, L"%d", value);
    RECT text_rect = chip;
    const HGDIOBJ old_font =
        data->m_combo_font ? SelectObject(hdc, data->m_combo_font.get())
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
      data->m_stroke_chip_rect.left - AnnotationEditorDividerGap / 2;
  drawToolbarDivider(hdc, divider_x, data->m_property_bar_rect.top + 8,
                     data->m_property_bar_rect.bottom - 8);

  const RECT& chip = data->m_stroke_chip_rect;
  const bool popup_open = strokePopupIsVisible(data);
  if (data->m_stroke_chip_hover || popup_open)
  {
    fillRoundRect(hdc, chip,
                  data->m_stroke_chip_hover ? colors.hover_fill
                                          : colors.selected_fill,
                  data->m_stroke_chip_hover ? colors.hover_fill
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
      data->m_combo_font ? SelectObject(hdc, data->m_combo_font.get()) : nullptr;
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, colors.label);
  DrawTextW(hdc, value_text, -1, &value,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  if (old_font != nullptr)
  {
    SelectObject(hdc, old_font);
  }
}

void paintEditorToolbar(HDC hdc, AnnotationEditorHost* data, bool draw_shell)
{
  if (hdc == nullptr || data == nullptr)
  {
    return;
  }

  if (draw_shell)
  {
    drawToolbarBar(hdc, data->m_main_bar_rect);
  }

  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    const EditorToolbarItem& item = data->m_toolbar_items[i];
    bool selected = false;
    switch (item.id)
    {
      case kButtonGeometryId:
        selected = annotationEditorIsGeometryTool(data->m_controller.tool());
        break;
      case kButtonArrowId:
        selected = data->m_controller.tool() == AnnotationTool::Arrow;
        break;
      case kButtonPenId:
        selected = data->m_controller.tool() == AnnotationTool::Pen;
        break;
      case kButtonMosaicId:
        selected = data->m_controller.tool() == AnnotationTool::Mosaic;
        break;
      case kButtonTextId:
        selected = data->m_controller.tool() == AnnotationTool::Text;
        break;
      default:
        break;
    }
    drawToolbarItem(hdc, item.rect, item.icon, i == data->m_toolbar_hover,
                    selected, true, item.accent,
                    item.id == kButtonGeometryId);
  }

  const int divider_top = data->m_main_bar_rect.top + 8;
  const int divider_bottom = data->m_main_bar_rect.bottom - 8;
  for (int i = 0; i < AnnotationEditorDividerCount; ++i)
  {
    if (data->m_toolbar_divider_x[i] > 0)
    {
      drawToolbarDivider(hdc, data->m_toolbar_divider_x[i], divider_top,
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

  data->m_font_combo = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"COMBOBOX", L"",
      WS_POPUP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0,
      AnnotationEditorFontComboWidth, AnnotationEditorFontComboDropHeight, hwnd,
      nullptr, GetModuleHandleW(nullptr), nullptr);
  if (data->m_font_combo == nullptr)
  {
    return false;
  }
  SetWindowLongPtrW(data->m_font_combo, GWLP_ID,
                    static_cast<LONG_PTR>(kFontComboId));

  data->m_combo_font.reset(CreateFontW(
      -kComboFontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE, AnnotationTextFontFace));
  if (data->m_combo_font)
  {
    SendMessageW(data->m_font_combo, WM_SETFONT,
                 reinterpret_cast<WPARAM>(data->m_combo_font.get()), TRUE);
  }

  data->m_controller.setFontSize(DefaultFontSize);
  data->m_controller.setMosaicBlockSize(DefaultMosaicBlockSize);
  fillSizeCombo(data);

  resizeEditorChrome(data);

  data->m_tooltip = createToolbarTooltip(hwnd);
  bindEditorTooltips(data);
  return true;
}

void invalidateToolbar(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_overlay == nullptr)
  {
    return;
  }
  InvalidateRect(data->m_overlay, nullptr, FALSE);
}

bool hitTestChromeBar(const AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr)
  {
    return false;
  }
  const POINT pt{x, y};
  if (PtInRect(&data->m_main_bar_rect, pt) != FALSE)
  {
    return true;
  }
  return PtInRect(&data->m_property_bar_rect, pt) != FALSE;
}

void bindEditorTooltips(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_overlay == nullptr || data->m_tooltip == nullptr)
  {
    return;
  }
  for (int i = 0; i < kToolbarIconItemCount; ++i)
  {
    const wchar_t* tip =
        (data->m_toolbar_items[static_cast<std::size_t>(i)].id ==
         kButtonGeometryId)
            ? toolbarIconLabel(ToolbarIconKind::Geometry)
            : toolbarIconLabel(
                  data->m_toolbar_items[static_cast<std::size_t>(i)].icon);
    bindToolbarTooltip(data->m_tooltip, data->m_overlay,
                       data->m_toolbar_items[static_cast<std::size_t>(i)].id,
                       data->m_toolbar_items[static_cast<std::size_t>(i)].rect,
                       tip, data->m_tooltip_text[static_cast<std::size_t>(i)],
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
  data->m_chrome_dragging = true;
  data->m_chrome_drag_start_x = cursor.x;
  data->m_chrome_drag_start_y = cursor.y;
  data->m_chrome_drag_origin_x = data->m_chrome_offset_x;
  data->m_chrome_drag_origin_y = data->m_chrome_offset_y;
  hideStrokePopup(data);
  SetCapture(hwnd);
}

void updateChromeDrag(AnnotationEditorHost* data, int /*x*/, int /*y*/)
{
  if (data == nullptr || !data->m_chrome_dragging || data->m_overlay == nullptr)
  {
    return;
  }
  POINT cursor{};
  if (GetCursorPos(&cursor) == FALSE)
  {
    return;
  }
  data->m_chrome_offset_x =
      data->m_chrome_drag_origin_x + (cursor.x - data->m_chrome_drag_start_x);
  data->m_chrome_offset_y =
      data->m_chrome_drag_origin_y + (cursor.y - data->m_chrome_drag_start_y);
  const RECT old_chrome =
      unionChromeRects(data->m_main_bar_rect, data->m_property_bar_rect);
  layoutEditorChrome(data->m_overlay, data);
  const RECT new_chrome =
      unionChromeRects(data->m_main_bar_rect, data->m_property_bar_rect);
  invalidateChromeMove(data->m_overlay, old_chrome, new_chrome);
}

void endChromeDrag(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  if (data->m_chrome_dragging)
  {
    data->m_chrome_dragging = false;
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
    if (PtInRect(&data->m_toolbar_items[i].rect, pt) != FALSE)
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
  if (id != kButtonTextId)
  {
    commitInlineText(data);
    clearTextSelection(data);
  }

  switch (id)
  {
    case kButtonGeometryId:
      data->m_controller.setTool(data->m_last_geometry_tool);
      break;
    case kButtonArrowId:
      data->m_controller.setTool(AnnotationTool::Arrow);
      break;
    case kButtonPenId:
      data->m_controller.setTool(AnnotationTool::Pen);
      break;
    case kButtonMosaicId:
      data->m_controller.setTool(AnnotationTool::Mosaic);
      break;
    case kButtonTextId:
      data->m_controller.setTool(AnnotationTool::Text);
      break;
    case kButtonUndoId:
      cancelInlineText(data);
      resetTextGesture(data);
      clearTextSelection(data);
      if (data->m_controller.undo(data->m_session.engine()))
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
