#include "annotate/annotation_editor_host.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include <commctrl.h>

namespace qingying {

namespace {

inline constexpr int kColorPickerEditTextMaxChars = 12;
inline constexpr int kColorPickerChannelCount = 3;
inline constexpr int kColorPickerAlphaTextMaxChars = 5;
inline constexpr int kColorPickerChannelTextMaxChars = 4;
inline constexpr int kColorPickerThumbSize = 10;
inline constexpr int kColorPickerSvCursorSize = 10;
inline constexpr int kColorPickerCheckerSize = 4;
inline constexpr int kColorPickerCloseInset = 3;
inline constexpr int kColorPickerComboDropHeight = 160;
inline constexpr COLORREF kColorPickerBgColor = RGB(255, 255, 255);
inline constexpr COLORREF kColorPickerTitleBg = RGB(248, 249, 251);
inline constexpr COLORREF kColorPickerBorderColor = RGB(226, 229, 234);
inline constexpr COLORREF kColorPickerTitleColor = RGB(55, 59, 66);
inline constexpr COLORREF kColorPickerCloseColor = RGB(72, 76, 84);
inline constexpr ColorBgra kCheckerDarkBgra{206, 202, 200, 255};
inline constexpr COLORREF kCursorRingColor = RGB(255, 255, 255);
inline constexpr COLORREF kCursorInnerColor = RGB(40, 44, 52);

const wchar_t kColorPickerTitleText[] = L"\x9009\x62E9\x989C\x8272";
const wchar_t kColorPickerOkText[] = L"\x786E\x8BA4";
const wchar_t kColorPickerCancelText[] = L"\x53D6\x6D88";
const wchar_t kColorPickerFormatHex[] = L"Hex";
const wchar_t kColorPickerFormatRgb[] = L"RGB";
const wchar_t kColorPickerFormatHsv[] = L"HSV";

int percentFromAlpha(std::uint8_t alpha)
{
  return (static_cast<int>(alpha) * ColorPercentMax + ColorChannelMax / 2) /
         ColorChannelMax;
}

std::uint8_t alphaFromPercent(int percent)
{
  return static_cast<std::uint8_t>(
      clampInt((percent * ColorChannelMax + ColorPercentMax / 2) /
                   ColorPercentMax,
               0, ColorChannelMax));
}

int valueToPos(int value, int left, int right, int max_value)
{
  const int width = right - left;
  if (width <= 0 || max_value <= 0)
  {
    return left;
  }
  const int clamped = clampInt(value, 0, max_value);
  return left + (clamped * width + max_value / 2) / max_value;
}

void virtualScreenBounds(int& left, int& top, int& right, int& bottom)
{
  left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
}

HWND createPickerChild(HWND parent, const wchar_t* class_name,
                        const wchar_t* text, DWORD style, const RECT& rect,
                        UINT id, HINSTANCE instance)
{
  return CreateWindowExW(WS_EX_CLIENTEDGE, class_name, text, style, rect.left,
                          rect.top, rect.right - rect.left,
                          rect.bottom - rect.top, parent,
                          reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)),
                          instance, nullptr);
}

void applyPickerFont(HWND control, const AnnotationEditorHost* data)
{
  if (control == nullptr || data == nullptr || !data->m_combo_font)
  {
    return;
  }
  SendMessageW(control, WM_SETFONT,
               reinterpret_cast<WPARAM>(data->m_combo_font.get()), TRUE);
}

}  // namespace

void moveColorPickerWindow(AnnotationEditorHost* data, int x, int y);
void syncColorPickerEdits(AnnotationEditorHost* data);
void updateColorPickerEditVisibility(AnnotationEditorHost* data);
void applyColorPickerEdit(AnnotationEditorHost* data, UINT id);
void applyColorPickerDraft(AnnotationEditorHost* data);
void invalidateColorPicker(AnnotationEditorHost* data);
void updateColorPickerFromPoint(AnnotationEditorHost* data, int x, int y,
                                 AnnotationEditorColorPickerHit hit);
bool registerColorPickerClass(HINSTANCE instance);
void paintColorPicker(HWND hwnd, AnnotationEditorHost* data);
void subclassPickerEdit(HWND edit, AnnotationEditorHost* data);
void unsubclassPickerEdit(HWND edit);
LRESULT CALLBACK colorPickerWndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                     LPARAM lparam);
LRESULT CALLBACK colorPickerEditSubclassProc(HWND hwnd, UINT msg,
                                               WPARAM wparam, LPARAM lparam,
                                               UINT_PTR subclass_id,
                                               DWORD_PTR ref_data);

void moveColorPickerWindow(AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr || data->m_color_picker == nullptr)
  {
    return;
  }
  int bound_left = 0;
  int bound_top = 0;
  int bound_right = 0;
  int bound_bottom = 0;
  virtualScreenBounds(bound_left, bound_top, bound_right, bound_bottom);
  annotationEditorClampRectOrigin(x, y, AnnotationEditorColorPickerWidth,
                                    AnnotationEditorColorPickerHeight, bound_left,
                                    bound_top, bound_right, bound_bottom);
  if (SetWindowPos(data->m_color_picker, HWND_TOPMOST, x, y, 0, 0,
                    SWP_NOSIZE | SWP_NOACTIVATE) == FALSE)
  {
    return;
  }
  data->m_color_picker_screen_x = x;
  data->m_color_picker_screen_y = y;
}

void applyColorPickerDraft(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_overlay == nullptr ||
      IsWindow(data->m_overlay) == FALSE)
  {
    return;
  }
  data->m_controller.setColor(data->m_color_picker_state.confirm());
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
}

void invalidateColorPicker(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_color_picker == nullptr)
  {
    return;
  }
  InvalidateRect(data->m_color_picker, nullptr, FALSE);
}

void blitArgbRect(HDC hdc, const RECT& dest, const void* bits, int width,
                   int height)
{
  if (hdc == nullptr || bits == nullptr || width <= 0 || height <= 0)
  {
    return;
  }
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  (void)StretchDIBits(hdc, dest.left, dest.top, width, height, 0, 0, width,
                       height, bits, &info, DIB_RGB_COLORS, SRCCOPY);
}

void paintSvPanel(HDC hdc, const RECT& sv, int hue, int saturation, int value)
{
  const int width = sv.right - sv.left;
  const int height = sv.bottom - sv.top;
  if (width <= 0 || height <= 0)
  {
    return;
  }
  void* bits = nullptr;
  const GdiObject dib(createTopDownArgbDib(width, height, &bits));
  if (!dib || bits == nullptr)
  {
    return;
  }
  std::uint32_t* pixels = static_cast<std::uint32_t*>(bits);
  for (int y = 0; y < height; ++y)
  {
    const int v = ColorPercentMax - annotationEditorColorPickerRatio(
                                          y, 0, height, ColorPercentMax);
    for (int x = 0; x < width; ++x)
    {
      const int s =
          annotationEditorColorPickerRatio(x, 0, width, ColorPercentMax);
      const ColorBgra color = hsvToRgb(
          hue, s, v, static_cast<std::uint8_t>(ColorChannelMax));
      pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)] = packColorBgra(color);
    }
  }
  blitArgbRect(hdc, sv, bits, width, height);

  const int cx = valueToPos(saturation, sv.left, sv.right, ColorPercentMax);
  const int cy =
      valueToPos(ColorPercentMax - value, sv.top, sv.bottom, ColorPercentMax);
  const int radius = kColorPickerSvCursorSize / 2;
  const GdiObject ring(CreatePen(PS_SOLID, 2, kCursorRingColor));
  const GdiObject inner(CreatePen(PS_SOLID, 1, kCursorInnerColor));
  const HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
  if (ring)
  {
    const SelectGuard pen(hdc, ring.get());
    Ellipse(hdc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
  }
  if (inner)
  {
    const SelectGuard pen(hdc, inner.get());
    Ellipse(hdc, cx - radius + 2, cy - radius + 2, cx + radius - 1,
            cy + radius - 1);
  }
  SelectObject(hdc, old_brush);
}

void paintHueBar(HDC hdc, const RECT& hue_rect, int hue)
{
  const int width = hue_rect.right - hue_rect.left;
  const int height = hue_rect.bottom - hue_rect.top;
  if (width <= 0 || height <= 0)
  {
    return;
  }
  void* bits = nullptr;
  const GdiObject dib(createTopDownArgbDib(width, height, &bits));
  if (!dib || bits == nullptr)
  {
    return;
  }
  std::uint32_t* pixels = static_cast<std::uint32_t*>(bits);
  for (int x = 0; x < width; ++x)
  {
    const int hue_x =
        annotationEditorColorPickerRatio(x, 0, width, ColorHueMaxDegrees);
    const ColorBgra color =
        hsvToRgb(hue_x, ColorPercentMax, ColorPercentMax,
                 static_cast<std::uint8_t>(ColorChannelMax));
    const std::uint32_t packed = packColorBgra(color);
    for (int y = 0; y < height; ++y)
    {
      pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)] = packed;
    }
  }
  blitArgbRect(hdc, hue_rect, bits, width, height);
  const int cx =
      valueToPos(hue, hue_rect.left, hue_rect.right, ColorHueMaxDegrees);
  const int cy = (hue_rect.top + hue_rect.bottom) / 2;
  const int radius = kColorPickerThumbSize / 2;
  const GdiObject pen(CreatePen(PS_SOLID, 1, kCursorInnerColor));
  const GdiObject brush(CreateSolidBrush(kCursorRingColor));
  if (pen && brush)
  {
    const SelectGuard p(hdc, pen.get());
    const SelectGuard b(hdc, brush.get());
    Ellipse(hdc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
  }
}

void paintAlphaBar(HDC hdc, const RECT& alpha_rect, const ColorBgra& draft)
{
  const int width = alpha_rect.right - alpha_rect.left;
  const int height = alpha_rect.bottom - alpha_rect.top;
  if (width <= 0 || height <= 0)
  {
    return;
  }
  void* bits = nullptr;
  const GdiObject dib(createTopDownArgbDib(width, height, &bits));
  if (!dib || bits == nullptr)
  {
    return;
  }
  std::uint32_t* pixels = static_cast<std::uint32_t*>(bits);
  const ColorBgra light{255, 255, 255, 255};
  const ColorBgra dark = kCheckerDarkBgra;
  for (int y = 0; y < height; ++y)
  {
    for (int x = 0; x < width; ++x)
    {
      const bool dark_cell =
          (((x / kColorPickerCheckerSize) + (y / kColorPickerCheckerSize)) %
           2) != 0;
      const ColorBgra bottom = dark_cell ? dark : light;
      ColorBgra src = draft;
      src.a = static_cast<std::uint8_t>(annotationEditorColorPickerRatio(
          x, 0, width, ColorChannelMax));
      pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)] =
          blendSrcOver(packColorBgra(bottom), src);
    }
  }
  blitArgbRect(hdc, alpha_rect, bits, width, height);
  const int cx = valueToPos(static_cast<int>(draft.a), alpha_rect.left,
                            alpha_rect.right, ColorChannelMax);
  const int cy = (alpha_rect.top + alpha_rect.bottom) / 2;
  const int radius = kColorPickerThumbSize / 2;
  const GdiObject pen(CreatePen(PS_SOLID, 1, kCursorInnerColor));
  const GdiObject brush(CreateSolidBrush(kCursorRingColor));
  if (pen && brush)
  {
    const SelectGuard p(hdc, pen.get());
    const SelectGuard b(hdc, brush.get());
    Ellipse(hdc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
  }
}

void paintEyedropper(HDC hdc, const RECT& cell, bool active)
{
  fillRoundRect(hdc, cell,
                active ? DefaultModernToolbarColors.selected_fill
                       : DefaultModernToolbarColors.hover_fill,
                kColorPickerBorderColor, kSwatchCornerRadius);
  drawToolbarIcon(hdc, cell, ToolbarIconKind::Eyedropper,
                  kColorPickerTitleColor);
}

void paintColorPicker(HWND hwnd, AnnotationEditorHost* data)
{
  if (hwnd == nullptr || data == nullptr)
  {
    return;
  }
  const PaintGuard paint(hwnd);
  if (paint.dc() == nullptr)
  {
    return;
  }
  RECT client{};
  GetClientRect(hwnd, &client);
  const GdiObject bg(CreateSolidBrush(kColorPickerBgColor));
  const GdiObject border(CreateSolidBrush(kColorPickerBorderColor));
  if (bg)
  {
    FillRect(paint.dc(), &client, bg.asBrush());
  }
  if (border)
  {
    FrameRect(paint.dc(), &client, border.asBrush());
  }

  const AnnotationEditorColorPickerLayout layout =
      annotationEditorColorPickerLayout();
  RECT title = toWinRect(layout.title);
  const GdiObject title_bg(CreateSolidBrush(kColorPickerTitleBg));
  if (title_bg)
  {
    FillRect(paint.dc(), &title, title_bg.asBrush());
  }
  SetBkMode(paint.dc(), TRANSPARENT);
  SetTextColor(paint.dc(), kColorPickerTitleColor);
  RECT title_text = title;
  title_text.left += AnnotationEditorColorPickerPadX;
  title_text.right = toWinRect(layout.close).left - AnnotationEditorButtonGap;
  HFONT font = data->m_combo_font.asFont();
  const HGDIOBJ old_font =
      (font != nullptr) ? SelectObject(paint.dc(), font) : nullptr;
  DrawTextW(paint.dc(), kColorPickerTitleText, -1, &title_text,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

  RECT close = toWinRect(layout.close);
  const GdiObject close_pen(CreatePen(PS_SOLID, 1, kColorPickerCloseColor));
  if (close_pen)
  {
    const SelectGuard p(paint.dc(), close_pen.get());
    MoveToEx(paint.dc(), close.left + kColorPickerCloseInset,
             close.top + kColorPickerCloseInset, nullptr);
    LineTo(paint.dc(), close.right - kColorPickerCloseInset,
           close.bottom - kColorPickerCloseInset);
    MoveToEx(paint.dc(), close.right - kColorPickerCloseInset - 1,
             close.top + kColorPickerCloseInset, nullptr);
    LineTo(paint.dc(), close.left + kColorPickerCloseInset,
           close.bottom - kColorPickerCloseInset);
  }
  if (old_font != nullptr)
  {
    SelectObject(paint.dc(), old_font);
  }

  const ColorBgra draft = data->m_color_picker_state.draft();
  const ColorHsv hsv = rgbToHsv(draft);
  paintSvPanel(paint.dc(), toWinRect(layout.sv),
               data->m_color_picker_state.hueDegrees(), hsv.saturation,
               hsv.value);
  paintHueBar(paint.dc(), toWinRect(layout.hue),
              data->m_color_picker_state.hueDegrees());
  paintAlphaBar(paint.dc(), toWinRect(layout.alpha), draft);
  paintEyedropper(paint.dc(), toWinRect(layout.eyedropper),
                  data->m_color_picker_eyedropping);
}

void setEditTextIfChanged(HWND edit, const wchar_t* text)
{
  if (edit == nullptr || text == nullptr)
  {
    return;
  }
  wchar_t current[kColorPickerEditTextMaxChars]{};
  if (GetWindowTextW(edit, current, kColorPickerEditTextMaxChars) < 0)
  {
    current[0] = L'\0';
  }
  if (lstrcmpW(current, text) == 0)
  {
    return;
  }
  (void)SetWindowTextW(edit, text);
}

void syncColorPickerEdits(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_color_picker_syncing)
  {
    return;
  }
  data->m_color_picker_syncing = true;
  const ColorBgra draft = data->m_color_picker_state.draft();
  const ColorPickerFormat format = data->m_color_picker_state.format();
  if (data->m_color_format_combo != nullptr)
  {
    const int wanted = static_cast<int>(format);
    const LRESULT current =
        SendMessageW(data->m_color_format_combo, CB_GETCURSEL, 0, 0);
    if (current != wanted)
    {
      SendMessageW(data->m_color_format_combo, CB_SETCURSEL,
                   static_cast<WPARAM>(wanted), 0);
    }
  }

  wchar_t hex[ColorHexTextMaxChars]{};
  if (formatHex(draft, hex, ColorHexTextMaxChars))
  {
    setEditTextIfChanged(data->m_color_hex_edit, hex);
  }

  wchar_t ch0[kColorPickerEditTextMaxChars]{};
  wchar_t ch1[kColorPickerEditTextMaxChars]{};
  wchar_t ch2[kColorPickerEditTextMaxChars]{};
  if (format == ColorPickerFormat::Hsv)
  {
    const ColorHsv hsv = rgbToHsv(draft);
    (void)swprintf_s(ch0, L"%d", data->m_color_picker_state.hueDegrees());
    (void)swprintf_s(ch1, L"%d", hsv.saturation);
    (void)swprintf_s(ch2, L"%d", hsv.value);
  }
  else
  {
    (void)swprintf_s(ch0, L"%u", static_cast<unsigned int>(draft.r));
    (void)swprintf_s(ch1, L"%u", static_cast<unsigned int>(draft.g));
    (void)swprintf_s(ch2, L"%u", static_cast<unsigned int>(draft.b));
  }
  setEditTextIfChanged(data->m_color_channel_edits[0], ch0);
  setEditTextIfChanged(data->m_color_channel_edits[1], ch1);
  setEditTextIfChanged(data->m_color_channel_edits[2], ch2);

  wchar_t alpha[kColorPickerEditTextMaxChars]{};
  (void)swprintf_s(alpha, L"%d%%", percentFromAlpha(draft.a));
  setEditTextIfChanged(data->m_color_alpha_edit, alpha);
  data->m_color_picker_syncing = false;
}

void updateColorPickerEditVisibility(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  const bool hex = data->m_color_picker_state.format() == ColorPickerFormat::Hex;
  if (data->m_color_hex_edit != nullptr)
  {
    ShowWindow(data->m_color_hex_edit, hex ? SW_SHOW : SW_HIDE);
  }
  for (int i = 0; i < kColorPickerChannelCount; ++i)
  {
    if (data->m_color_channel_edits[static_cast<std::size_t>(i)] != nullptr)
    {
      ShowWindow(data->m_color_channel_edits[static_cast<std::size_t>(i)],
                 hex ? SW_HIDE : SW_SHOW);
    }
  }
}

void applyColorPickerEdit(AnnotationEditorHost* data, UINT id)
{
  if (data == nullptr || data->m_color_picker_syncing)
  {
    return;
  }
  wchar_t text[kColorPickerEditTextMaxChars]{};
  HWND edit = nullptr;
  if (id == kColorPickerHexEditId)
  {
    edit = data->m_color_hex_edit;
  }
  else if (id == kColorPickerAlphaEditId)
  {
    edit = data->m_color_alpha_edit;
  }
  else if (id >= kColorPickerChannel0Id && id <= kColorPickerChannel2Id)
  {
    edit = data->m_color_channel_edits[id - kColorPickerChannel0Id];
  }
  if (edit == nullptr)
  {
    return;
  }
  if (GetWindowTextW(edit, text, kColorPickerEditTextMaxChars) < 0)
  {
    text[0] = L'\0';
  }

  bool ok = false;
  if (id == kColorPickerHexEditId)
  {
    ColorBgra color = data->m_color_picker_state.draft();
    if (parseHex(text, color))
    {
      data->m_color_picker_state.setRgb(color.r, color.g, color.b);
      ok = true;
    }
  }
  else if (id == kColorPickerAlphaEditId)
  {
    int percent = 0;
    if (parsePercent(text, percent))
    {
      data->m_color_picker_state.setAlpha(alphaFromPercent(percent));
      ok = true;
    }
  }
  else
  {
    const int channel = static_cast<int>(id - kColorPickerChannel0Id);
    int value = 0;
    if (data->m_color_picker_state.format() == ColorPickerFormat::Hsv)
    {
      if (channel == 0)
      {
        ok = parseHue(text, value);
        if (ok)
        {
          data->m_color_picker_state.setHueDegrees(value);
        }
      }
      else if (parsePercent(text, value))
      {
        const ColorHsv hsv = rgbToHsv(data->m_color_picker_state.draft());
        const int sat = (channel == 1) ? value : hsv.saturation;
        const int val = (channel == 2) ? value : hsv.value;
        data->m_color_picker_state.setSaturationValue(sat, val);
        ok = true;
      }
    }
    else if (parseChannel255(text, value))
    {
      ColorBgra draft = data->m_color_picker_state.draft();
      if (channel == 0)
      {
        draft.r = static_cast<std::uint8_t>(value);
      }
      else if (channel == 1)
      {
        draft.g = static_cast<std::uint8_t>(value);
      }
      else
      {
        draft.b = static_cast<std::uint8_t>(value);
      }
      data->m_color_picker_state.setRgb(draft.r, draft.g, draft.b);
      ok = true;
    }
  }

  if (!ok)
  {
    syncColorPickerEdits(data);
    return;
  }
  syncColorPickerEdits(data);
  invalidateColorPicker(data);
}

void updateColorPickerFromPoint(AnnotationEditorHost* data, int x, int y,
                                 AnnotationEditorColorPickerHit hit)
{
  if (data == nullptr)
  {
    return;
  }
  const AnnotationEditorColorPickerLayout layout =
      annotationEditorColorPickerLayout();
  if (hit == AnnotationEditorColorPickerHit::Sv)
  {
    int saturation = 0;
    int value = 0;
    annotationEditorColorPickerSvAt(layout.sv, x, y, saturation, value);
    data->m_color_picker_state.setSaturationValue(saturation, value);
  }
  else if (hit == AnnotationEditorColorPickerHit::Hue)
  {
    data->m_color_picker_state.setHueDegrees(annotationEditorColorPickerRatio(
        x, layout.hue.left, layout.hue.right, ColorHueMaxDegrees));
  }
  else if (hit == AnnotationEditorColorPickerHit::Alpha)
  {
    data->m_color_picker_state.setAlpha(static_cast<std::uint8_t>(
        annotationEditorColorPickerRatio(x, layout.alpha.left,
                                          layout.alpha.right,
                                          ColorChannelMax)));
  }
  else
  {
    return;
  }
  syncColorPickerEdits(data);
  invalidateColorPicker(data);
}

void handleColorPickerCommand(AnnotationEditorHost* data, UINT id, UINT code)
{
  if (data == nullptr)
  {
    return;
  }
  if (code == BN_CLICKED)
  {
    if (id == kColorPickerOkId)
    {
      hideColorPicker(data, true);
    }
    else if (id == kColorPickerCancelId)
    {
      hideColorPicker(data, false);
    }
    return;
  }
  if (code == CBN_SELCHANGE && id == kColorPickerFormatComboId)
  {
    if (data->m_color_picker_syncing || data->m_color_format_combo == nullptr)
    {
      return;
    }
    const LRESULT sel =
        SendMessageW(data->m_color_format_combo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR || sel < 0)
    {
      return;
    }
    ColorPickerFormat format = ColorPickerFormat::Hex;
    if (sel == 1)
    {
      format = ColorPickerFormat::Rgb;
    }
    else if (sel == 2)
    {
      format = ColorPickerFormat::Hsv;
    }
    data->m_color_picker_state.setFormat(format);
    updateColorPickerEditVisibility(data);
    syncColorPickerEdits(data);
    return;
  }
  if (code == EN_KILLFOCUS)
  {
    applyColorPickerEdit(data, id);
  }
}

LRESULT CALLBACK colorPickerEditSubclassProc(HWND hwnd, UINT msg,
                                               WPARAM wparam, LPARAM lparam,
                                               UINT_PTR subclass_id,
                                               DWORD_PTR ref_data)
{
  AnnotationEditorHost* data =
      reinterpret_cast<AnnotationEditorHost*>(ref_data);
  if (msg == WM_NCDESTROY)
  {
    (void)RemoveWindowSubclass(hwnd, colorPickerEditSubclassProc, subclass_id);
    return DefSubclassProc(hwnd, msg, wparam, lparam);
  }
  if (msg == WM_KEYDOWN && data != nullptr)
  {
    if (wparam == VK_RETURN)
    {
      applyColorPickerEdit(data, static_cast<UINT>(GetDlgCtrlID(hwnd)));
      hideColorPicker(data, true);
      return 0;
    }
    if (wparam == VK_ESCAPE)
    {
      if (data->m_color_picker_eyedropping)
      {
        data->m_color_picker_eyedropping = false;
        invalidateColorPicker(data);
        return 0;
      }
      hideColorPicker(data, false);
      return 0;
    }
  }
  return DefSubclassProc(hwnd, msg, wparam, lparam);
}

void subclassPickerEdit(HWND edit, AnnotationEditorHost* data)
{
  if (edit == nullptr || data == nullptr)
  {
    return;
  }
  (void)SetWindowSubclass(edit, colorPickerEditSubclassProc,
                           kColorPickerEditSubclassId,
                           reinterpret_cast<DWORD_PTR>(data));
}

void unsubclassPickerEdit(HWND edit)
{
  if (edit == nullptr)
  {
    return;
  }
  (void)RemoveWindowSubclass(edit, colorPickerEditSubclassProc,
                              kColorPickerEditSubclassId);
}

LRESULT CALLBACK colorPickerWndProc(HWND hwnd, UINT msg, WPARAM wparam,
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
    case WM_PAINT:
      paintColorPicker(hwnd, data);
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_COMMAND:
      if (data != nullptr)
      {
        handleColorPickerCommand(data, LOWORD(wparam), HIWORD(wparam));
      }
      return 0;
    case WM_KEYDOWN:
      if (data != nullptr)
      {
        if (wparam == VK_RETURN)
        {
          hideColorPicker(data, true);
          return 0;
        }
        if (wparam == VK_ESCAPE)
        {
          if (data->m_color_picker_eyedropping)
          {
            data->m_color_picker_eyedropping = false;
            invalidateColorPicker(data);
            return 0;
          }
          hideColorPicker(data, false);
          return 0;
        }
      }
      return 0;
    case WM_LBUTTONDOWN:
    {
      if (data == nullptr)
      {
        return 0;
      }
      const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
      const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
      const AnnotationEditorColorPickerLayout layout =
          annotationEditorColorPickerLayout();
      const AnnotationEditorColorPickerHit hit =
          annotationEditorHitColorPicker(layout, x, y);
      if (hit == AnnotationEditorColorPickerHit::Close)
      {
        hideColorPicker(data, false);
        return 0;
      }
      if (hit == AnnotationEditorColorPickerHit::Eyedropper)
      {
        data->m_color_picker_eyedropping = !data->m_color_picker_eyedropping;
        invalidateColorPicker(data);
        return 0;
      }
      data->m_color_picker_eyedropping = false;
      if (hit == AnnotationEditorColorPickerHit::Title)
      {
        POINT cursor{};
        if (GetCursorPos(&cursor) != FALSE)
        {
          RECT window{};
          GetWindowRect(hwnd, &window);
          data->m_color_picker_dragging = true;
          data->m_color_picker_drag_start_x = cursor.x;
          data->m_color_picker_drag_start_y = cursor.y;
          data->m_color_picker_drag_origin_x = window.left;
          data->m_color_picker_drag_origin_y = window.top;
          SetCapture(hwnd);
        }
        return 0;
      }
      if (hit == AnnotationEditorColorPickerHit::Sv ||
          hit == AnnotationEditorColorPickerHit::Hue ||
          hit == AnnotationEditorColorPickerHit::Alpha)
      {
        data->m_color_sv_dragging = hit == AnnotationEditorColorPickerHit::Sv;
        data->m_color_hue_dragging = hit == AnnotationEditorColorPickerHit::Hue;
        data->m_color_alpha_dragging =
            hit == AnnotationEditorColorPickerHit::Alpha;
        SetCapture(hwnd);
        updateColorPickerFromPoint(data, x, y, hit);
      }
      return 0;
    }
    case WM_MOUSEMOVE:
      if (data == nullptr)
      {
        return 0;
      }
      if (data->m_color_picker_dragging)
      {
        POINT cursor{};
        if (GetCursorPos(&cursor) != FALSE)
        {
          moveColorPickerWindow(
              data,
              data->m_color_picker_drag_origin_x +
                  (cursor.x - data->m_color_picker_drag_start_x),
              data->m_color_picker_drag_origin_y +
                  (cursor.y - data->m_color_picker_drag_start_y));
          data->m_color_picker_moved = true;
        }
        return 0;
      }
      if (data->m_color_sv_dragging || data->m_color_hue_dragging ||
          data->m_color_alpha_dragging)
      {
        const int x = static_cast<int>(static_cast<short>(LOWORD(lparam)));
        const int y = static_cast<int>(static_cast<short>(HIWORD(lparam)));
        AnnotationEditorColorPickerHit hit = AnnotationEditorColorPickerHit::Sv;
        if (data->m_color_hue_dragging)
        {
          hit = AnnotationEditorColorPickerHit::Hue;
        }
        else if (data->m_color_alpha_dragging)
        {
          hit = AnnotationEditorColorPickerHit::Alpha;
        }
        updateColorPickerFromPoint(data, x, y, hit);
      }
      return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
      if (data != nullptr)
      {
        data->m_color_picker_dragging = false;
        data->m_color_sv_dragging = false;
        data->m_color_hue_dragging = false;
        data->m_color_alpha_dragging = false;
      }
      if (msg == WM_LBUTTONUP)
      {
        ReleaseCapture();
      }
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool registerColorPickerClass(HINSTANCE instance)
{
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(WNDCLASSEXW);
  wc.lpfnWndProc = colorPickerWndProc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
  wc.lpszClassName = kColorPickerClassName;
  return RegisterClassExW(&wc) != 0 ||
         GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool colorPickerIsVisible(const AnnotationEditorHost* data)
{
  return data != nullptr && data->m_color_picker != nullptr &&
         IsWindowVisible(data->m_color_picker) != FALSE;
}

void hideColorPicker(AnnotationEditorHost* data, bool apply)
{
  if (data == nullptr)
  {
    return;
  }
  data->m_color_picker_dragging = false;
  data->m_color_sv_dragging = false;
  data->m_color_hue_dragging = false;
  data->m_color_alpha_dragging = false;
  data->m_color_picker_eyedropping = false;
  if (apply)
  {
    applyColorPickerDraft(data);
  }
  if (data->m_color_picker != nullptr)
  {
    if (ShowWindow(data->m_color_picker, SW_HIDE) == 0)
    {
      // 已隐藏时返回 0。
    }
  }
  if (data->m_overlay != nullptr)
  {
    (void)SetFocus(data->m_overlay);
    invalidateToolbar(data);
  }
}

void destroyColorPicker(AnnotationEditorHost* data)
{
  if (data == nullptr)
  {
    return;
  }
  data->m_color_picker_dragging = false;
  data->m_color_sv_dragging = false;
  data->m_color_hue_dragging = false;
  data->m_color_alpha_dragging = false;
  data->m_color_picker_eyedropping = false;
  unsubclassPickerEdit(data->m_color_hex_edit);
  unsubclassPickerEdit(data->m_color_alpha_edit);
  for (int i = 0; i < kColorPickerChannelCount; ++i)
  {
    unsubclassPickerEdit(data->m_color_channel_edits[static_cast<std::size_t>(i)]);
  }
  if (data->m_color_picker != nullptr)
  {
    DestroyWindow(data->m_color_picker);
  }
  data->m_color_picker = nullptr;
  data->m_color_format_combo = nullptr;
  data->m_color_hex_edit = nullptr;
  data->m_color_channel_edits[0] = nullptr;
  data->m_color_channel_edits[1] = nullptr;
  data->m_color_channel_edits[2] = nullptr;
  data->m_color_alpha_edit = nullptr;
  data->m_color_ok = nullptr;
  data->m_color_cancel = nullptr;
}

void showColorPicker(AnnotationEditorHost* data)
{
  if (data == nullptr || data->m_overlay == nullptr ||
      !annotationEditorPropertyBarShowsColor(data->m_controller.tool()))
  {
    return;
  }

  hideStrokePopup(data);
  data->m_color_picker_state.open(data->m_controller.style().color);
  data->m_color_picker_eyedropping = false;

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  if (!registerColorPickerClass(instance))
  {
    return;
  }

  RECT swatch = data->m_current_color_rect;
  POINT anchor{swatch.left, swatch.top};
  ClientToScreen(data->m_overlay, &anchor);
  int bound_left = 0;
  int bound_top = 0;
  int bound_right = 0;
  int bound_bottom = 0;
  virtualScreenBounds(bound_left, bound_top, bound_right, bound_bottom);

  int x = 0;
  int y = 0;
  if (data->m_color_picker_moved)
  {
    x = data->m_color_picker_screen_x;
    y = data->m_color_picker_screen_y;
    annotationEditorClampRectOrigin(x, y, AnnotationEditorColorPickerWidth,
                                      AnnotationEditorColorPickerHeight,
                                      bound_left, bound_top, bound_right,
                                      bound_bottom);
  }
  else
  {
    annotationEditorPlaceColorPicker(
        anchor.x, anchor.y, swatch.right - swatch.left,
        swatch.bottom - swatch.top, bound_left, bound_top, bound_right,
        bound_bottom, x, y);
  }

  const AnnotationEditorColorPickerLayout layout =
      annotationEditorColorPickerLayout();
  if (data->m_color_picker == nullptr)
  {
    data->m_color_picker = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kColorPickerClassName, L"",
        WS_POPUP | WS_CLIPCHILDREN, x, y, AnnotationEditorColorPickerWidth,
        AnnotationEditorColorPickerHeight, data->m_overlay, nullptr, instance,
        data);
    if (data->m_color_picker == nullptr)
    {
      return;
    }

    RECT format = toWinRect(layout.format);
    data->m_color_format_combo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, format.left,
        format.top, format.right - format.left, kColorPickerComboDropHeight,
        data->m_color_picker,
        reinterpret_cast<HMENU>(
            static_cast<UINT_PTR>(kColorPickerFormatComboId)),
        instance, nullptr);
    if (data->m_color_format_combo != nullptr)
    {
      SendMessageW(data->m_color_format_combo, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(kColorPickerFormatHex));
      SendMessageW(data->m_color_format_combo, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(kColorPickerFormatRgb));
      SendMessageW(data->m_color_format_combo, CB_ADDSTRING, 0,
                   reinterpret_cast<LPARAM>(kColorPickerFormatHsv));
    }

    data->m_color_hex_edit = createPickerChild(
        data->m_color_picker, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, toWinRect(layout.hex_edit),
        kColorPickerHexEditId, instance);
    for (int i = 0; i < kColorPickerChannelCount; ++i)
    {
      data->m_color_channel_edits[static_cast<std::size_t>(i)] =
          createPickerChild(
              data->m_color_picker, L"EDIT", L"",
              WS_CHILD | ES_AUTOHSCROLL | ES_NUMBER,
              toWinRect(layout.channel_edits[static_cast<std::size_t>(i)]),
              kColorPickerChannel0Id + static_cast<UINT>(i), instance);
    }
    data->m_color_alpha_edit = createPickerChild(
        data->m_color_picker, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        toWinRect(layout.alpha_edit), kColorPickerAlphaEditId, instance);
    data->m_color_ok = CreateWindowExW(
        0, L"BUTTON", kColorPickerOkText, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        layout.confirm.left, layout.confirm.top,
        layout.confirm.right - layout.confirm.left,
        layout.confirm.bottom - layout.confirm.top, data->m_color_picker,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kColorPickerOkId)),
        instance, nullptr);
    data->m_color_cancel = CreateWindowExW(
        0, L"BUTTON", kColorPickerCancelText,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, layout.cancel.left,
        layout.cancel.top, layout.cancel.right - layout.cancel.left,
        layout.cancel.bottom - layout.cancel.top, data->m_color_picker,
        reinterpret_cast<HMENU>(static_cast<UINT_PTR>(kColorPickerCancelId)),
        instance, nullptr);

    applyPickerFont(data->m_color_format_combo, data);
    applyPickerFont(data->m_color_hex_edit, data);
    applyPickerFont(data->m_color_alpha_edit, data);
    applyPickerFont(data->m_color_ok, data);
    applyPickerFont(data->m_color_cancel, data);
    for (int i = 0; i < kColorPickerChannelCount; ++i)
    {
      applyPickerFont(data->m_color_channel_edits[static_cast<std::size_t>(i)],
                      data);
    }
    if (data->m_color_hex_edit != nullptr)
    {
      SendMessageW(data->m_color_hex_edit, EM_SETLIMITTEXT,
                   ColorHexTextMaxChars - 1, 0);
    }
    if (data->m_color_alpha_edit != nullptr)
    {
      SendMessageW(data->m_color_alpha_edit, EM_SETLIMITTEXT,
                   kColorPickerAlphaTextMaxChars, 0);
    }
    subclassPickerEdit(data->m_color_hex_edit, data);
    subclassPickerEdit(data->m_color_alpha_edit, data);
    for (int i = 0; i < kColorPickerChannelCount; ++i)
    {
      HWND channel = data->m_color_channel_edits[static_cast<std::size_t>(i)];
      if (channel != nullptr)
      {
        SendMessageW(channel, EM_SETLIMITTEXT, kColorPickerChannelTextMaxChars,
                     0);
        subclassPickerEdit(channel, data);
      }
    }
  }
  else
  {
    moveColorPickerWindow(data, x, y);
  }

  data->m_color_picker_screen_x = x;
  data->m_color_picker_screen_y = y;
  updateColorPickerEditVisibility(data);
  syncColorPickerEdits(data);
  if (ShowWindow(data->m_color_picker, SW_SHOW) == 0)
  {
    // 先前已显示时返回 0。
  }
  (void)SetForegroundWindow(data->m_color_picker);
  invalidateColorPicker(data);
}

bool handleColorPickerEyedropperClick(AnnotationEditorHost* data, int x, int y)
{
  if (data == nullptr || !data->m_color_picker_eyedropping)
  {
    return false;
  }
  if (!pointInImageArea(data, x, y))
  {
    return true;
  }
  const int image_x = x - data->m_image_origin_x;
  const int image_y = y - data->m_image_origin_y;
  ColorBgra sampled{};
  if (!sampleImageBgra(data->m_session.source(), image_x, image_y, sampled))
  {
    return true;
  }
  data->m_color_picker_state.sampleOpaqueRgb(sampled);
  data->m_color_picker_eyedropping = false;
  syncColorPickerEdits(data);
  invalidateColorPicker(data);
  return true;
}

}  // namespace qingying
