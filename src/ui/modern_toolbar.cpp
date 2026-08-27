#include "qingying/ui/modern_toolbar.hpp"

#include <algorithm>
#include <cwchar>

#include <commctrl.h>

namespace qingying {

namespace {

class PenGuard
{
 public:
  PenGuard(HDC hdc, int width, COLORREF color) : m_hdc(hdc)
  {
    LOGBRUSH brush{};
    brush.lbStyle = BS_SOLID;
    brush.lbColor = color;
    m_pen = ExtCreatePen(PS_GEOMETRIC | PS_SOLID | PS_ENDCAP_SQUARE |
                             PS_JOIN_MITER,
                         (std::max)(1, width), &brush, 0, nullptr);
    if (m_pen != nullptr)
    {
      m_old = SelectObject(m_hdc, m_pen);
    }
  }

  ~PenGuard()
  {
    if (m_pen != nullptr)
    {
      SelectObject(m_hdc, m_old);
      DeleteObject(m_pen);
    }
  }

  PenGuard(const PenGuard&) = delete;
  PenGuard& operator=(const PenGuard&) = delete;

  bool ok() const
  {
    return m_pen != nullptr;
  }

 private:
  HDC m_hdc{nullptr};
  HPEN m_pen{nullptr};
  HGDIOBJ m_old{nullptr};
};

POINT centerOf(const RECT& cell)
{
  return POINT{(cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2};
}

int iconHalfExtent(const RECT& cell)
{
  const int w = cell.right - cell.left;
  const int h = cell.bottom - cell.top;
  const int cell_size = (std::min)(w, h);
  return (std::max)(5, (cell_size * 6) / 25);
}

void lineTo(HDC hdc, int x, int y)
{
  LineTo(hdc, x, y);
}

void copyWide(wchar_t* dest, std::size_t dest_chars, const wchar_t* source)
{
  if (dest == nullptr || dest_chars == 0)
  {
    return;
  }
  dest[0] = L'\0';
  if (source == nullptr)
  {
    return;
  }
  wcsncpy_s(dest, dest_chars, source, _TRUNCATE);
}

}  // namespace

void fillRoundRect(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border,
                   int radius)
{
  if (hdc == nullptr)
  {
    return;
  }

  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  const int half = (std::min)(width, height) / 2;
  const int safe_radius = (std::max)(0, (std::min)(radius, half));
  const HBRUSH brush = CreateSolidBrush(fill);
  const HPEN pen = CreatePen(PS_SOLID, 1, border);
  if (brush == nullptr || pen == nullptr)
  {
    if (brush != nullptr)
    {
      DeleteObject(brush);
    }
    if (pen != nullptr)
    {
      DeleteObject(pen);
    }
    return;
  }

  const HGDIOBJ old_brush = SelectObject(hdc, brush);
  const HGDIOBJ old_pen = SelectObject(hdc, pen);
  RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, safe_radius * 2,
            safe_radius * 2);
  SelectObject(hdc, old_pen);
  SelectObject(hdc, old_brush);
  DeleteObject(pen);
  DeleteObject(brush);
}

void drawToolbarBar(HDC hdc, const RECT& rect)
{
  if (hdc == nullptr)
  {
    return;
  }

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const ModernToolbarMetrics metrics = DefaultModernToolbarMetrics;
  RECT shadow = rect;
  OffsetRect(&shadow, 0, 1);
  fillRoundRect(hdc, shadow, colors.bar_shadow, colors.bar_shadow,
                metrics.corner_radius);
  fillRoundRect(hdc, rect, colors.bar_fill, colors.bar_border,
                metrics.corner_radius);
}

void drawToolbarIcon(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     COLORREF color)
{
  if (hdc == nullptr)
  {
    return;
  }

  const PenGuard pen(hdc, 1, color);
  if (!pen.ok())
  {
    return;
  }

  SetBkMode(hdc, TRANSPARENT);
  const HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
  const POINT c = centerOf(cell);
  const int s = iconHalfExtent(cell);

  switch (kind)
  {
    case ToolbarIconKind::Copy:
      Rectangle(hdc, c.x - s + 3, c.y - s, c.x + s - 1, c.y + s - 4);
      Rectangle(hdc, c.x - s, c.y - s + 4, c.x + s - 4, c.y + s);
      break;
    case ToolbarIconKind::Save:
      MoveToEx(hdc, c.x, c.y - s, nullptr);
      lineTo(hdc, c.x, c.y + 1);
      MoveToEx(hdc, c.x - 3, c.y - 2, nullptr);
      lineTo(hdc, c.x, c.y + 1);
      lineTo(hdc, c.x + 3, c.y - 2);
      MoveToEx(hdc, c.x - s, c.y + 3, nullptr);
      lineTo(hdc, c.x - s, c.y + s);
      lineTo(hdc, c.x + s, c.y + s);
      lineTo(hdc, c.x + s, c.y + 3);
      break;
    case ToolbarIconKind::Edit:
      MoveToEx(hdc, c.x - s + 1, c.y + s - 1, nullptr);
      lineTo(hdc, c.x + s - 3, c.y - s + 3);
      MoveToEx(hdc, c.x + s - 5, c.y - s + 1, nullptr);
      lineTo(hdc, c.x + s - 1, c.y - s + 5);
      lineTo(hdc, c.x + s - 3, c.y - s + 3);
      MoveToEx(hdc, c.x - s + 1, c.y + s - 1, nullptr);
      lineTo(hdc, c.x - s + 4, c.y + s);
      lineTo(hdc, c.x - s, c.y + s - 3);
      break;
    case ToolbarIconKind::Pin:
      Ellipse(hdc, c.x - 3, c.y - s, c.x + 3, c.y - s + 6);
      MoveToEx(hdc, c.x - s + 2, c.y - 2, nullptr);
      lineTo(hdc, c.x + s - 2, c.y - 2);
      MoveToEx(hdc, c.x, c.y - 2, nullptr);
      lineTo(hdc, c.x, c.y + s);
      break;
    case ToolbarIconKind::Rectangle:
      Rectangle(hdc, c.x - s + 1, c.y - s + 2, c.x + s - 1, c.y + s - 2);
      break;
    case ToolbarIconKind::Ellipse:
      Ellipse(hdc, c.x - s + 1, c.y - s + 2, c.x + s - 1, c.y + s - 2);
      break;
    case ToolbarIconKind::Geometry:
      Rectangle(hdc, c.x - s + 1, c.y - s + 2, c.x + s - 3, c.y + s - 2);
      break;
    case ToolbarIconKind::Fill:
      Rectangle(hdc, c.x - s + 2, c.y - s + 3, c.x + s - 2, c.y + s - 3);
      break;
    case ToolbarIconKind::LineSolid:
    case ToolbarIconKind::LineDashed:
    case ToolbarIconKind::LineDotted:
    {
      const int style =
          (kind == ToolbarIconKind::LineDashed)
              ? PS_DASH
              : ((kind == ToolbarIconKind::LineDotted) ? PS_DOT : PS_SOLID);
      const HPEN dash_pen = CreatePen(style, 1, color);
      if (dash_pen != nullptr)
      {
        const HGDIOBJ old_dash = SelectObject(hdc, dash_pen);
        MoveToEx(hdc, c.x - s + 1, c.y, nullptr);
        lineTo(hdc, c.x + s - 1, c.y);
        SelectObject(hdc, old_dash);
        DeleteObject(dash_pen);
      }
      break;
    }
    case ToolbarIconKind::Arrow:
      MoveToEx(hdc, c.x - s + 1, c.y + s - 2, nullptr);
      lineTo(hdc, c.x + s - 2, c.y - s + 2);
      MoveToEx(hdc, c.x + 1, c.y - s + 2, nullptr);
      lineTo(hdc, c.x + s - 2, c.y - s + 2);
      lineTo(hdc, c.x + s - 2, c.y + 1);
      break;
    case ToolbarIconKind::Pen:
      MoveToEx(hdc, c.x - s + 1, c.y + s - 2, nullptr);
      lineTo(hdc, c.x + 2, c.y - s + 3);
      lineTo(hdc, c.x + s - 2, c.y - s + 6);
      lineTo(hdc, c.x + 1, c.y + s - 4);
      lineTo(hdc, c.x - s + 1, c.y + s - 2);
      break;
    case ToolbarIconKind::Mosaic:
    {
      // 2x2 小方格，表示像素化打码。
      const int grid = (std::max)(2, s / 2);
      Rectangle(hdc, c.x - grid, c.y - grid, c.x, c.y);
      Rectangle(hdc, c.x, c.y - grid, c.x + grid, c.y);
      Rectangle(hdc, c.x - grid, c.y, c.x, c.y + grid);
      Rectangle(hdc, c.x, c.y, c.x + grid, c.y + grid);
      break;
    }
    case ToolbarIconKind::Text:
      MoveToEx(hdc, c.x - s + 2, c.y - s + 2, nullptr);
      lineTo(hdc, c.x + s - 2, c.y - s + 2);
      MoveToEx(hdc, c.x, c.y - s + 2, nullptr);
      lineTo(hdc, c.x, c.y + s - 2);
      break;
    case ToolbarIconKind::Undo:
      Arc(hdc, c.x - s + 1, c.y - s + 2, c.x + s - 1, c.y + s - 1, c.x + s - 2,
          c.y, c.x - 1, c.y - s + 3);
      MoveToEx(hdc, c.x - s + 1, c.y - 1, nullptr);
      lineTo(hdc, c.x - 2, c.y - s + 3);
      MoveToEx(hdc, c.x - s + 1, c.y - 1, nullptr);
      lineTo(hdc, c.x - 1, c.y + 3);
      break;
    case ToolbarIconKind::Confirm:
      MoveToEx(hdc, c.x - s + 2, c.y, nullptr);
      lineTo(hdc, c.x - 1, c.y + s - 4);
      lineTo(hdc, c.x + s - 2, c.y - s + 3);
      break;
    case ToolbarIconKind::Cancel:
      MoveToEx(hdc, c.x - s + 3, c.y - s + 3, nullptr);
      lineTo(hdc, c.x + s - 3, c.y + s - 3);
      MoveToEx(hdc, c.x + s - 3, c.y - s + 3, nullptr);
      lineTo(hdc, c.x - s + 3, c.y + s - 3);
      break;
    default:
      break;
  }

  SelectObject(hdc, old_brush);
}

constexpr int kChevronInsetRightPx = 7;
constexpr int kChevronInsetBottomPx = 6;
constexpr int kChevronArmPx = 3;
constexpr int kChevronDropPx = 2;

void drawToolbarItem(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     bool hovered, bool selected, bool enabled, bool accent,
                     bool grouped)
{
  if (hdc == nullptr)
  {
    return;
  }

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const ModernToolbarMetrics metrics = DefaultModernToolbarMetrics;
  COLORREF fill = colors.button_fill;
  if (!enabled)
  {
    fill = colors.disabled_fill;
  }
  else if (hovered)
  {
    fill = colors.hover_fill;
  }
  else if (selected || accent)
  {
    fill = colors.selected_fill;
  }

  fillRoundRect(hdc, cell, fill, fill, metrics.hover_radius);

  const COLORREF icon_color = enabled ? colors.icon : colors.icon_disabled;
  drawToolbarIcon(hdc, cell, kind, icon_color);

  if (grouped)
  {
    const int chevron_x = cell.right - kChevronInsetRightPx;
    const int chevron_y = cell.bottom - kChevronInsetBottomPx;
    const HPEN pen = CreatePen(PS_SOLID, 1, icon_color);
    if (pen != nullptr)
    {
      const HGDIOBJ old_pen = SelectObject(hdc, pen);
      MoveToEx(hdc, chevron_x, chevron_y, nullptr);
      LineTo(hdc, chevron_x + kChevronArmPx, chevron_y);
      LineTo(hdc, chevron_x + kChevronArmPx / 2, chevron_y + kChevronDropPx);
      LineTo(hdc, chevron_x, chevron_y);
      SelectObject(hdc, old_pen);
      DeleteObject(pen);
    }
  }
}

void drawToolbarDivider(HDC hdc, int x, int top, int bottom)
{
  if (hdc == nullptr)
  {
    return;
  }

  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const PenGuard pen(hdc, 1, colors.divider);
  if (!pen.ok())
  {
    return;
  }
  MoveToEx(hdc, x, top, nullptr);
  lineTo(hdc, x, bottom);
}

const wchar_t* toolbarIconLabel(ToolbarIconKind kind)
{
  // 使用码点转义，避免源文件编码导致 Tooltip 显示乱码。
  switch (kind)
  {
    case ToolbarIconKind::Copy:
      return L"\x590D\x5236";
    case ToolbarIconKind::Save:
      return L"\x4E0B\x8F7D\x56FE\x7247";
    case ToolbarIconKind::Edit:
      return L"\x7F16\x8F91";
    case ToolbarIconKind::Pin:
      return L"\x9489\x56FE";
    case ToolbarIconKind::Rectangle:
      return L"\x77E9\x5F62";
    case ToolbarIconKind::Ellipse:
      return L"\x692D\x5706";
    case ToolbarIconKind::Geometry:
      return L"\x51E0\x4F55";
    case ToolbarIconKind::Fill:
      return L"\x586B\x5145";
    case ToolbarIconKind::LineSolid:
      return L"\x5B9E\x7EBF";
    case ToolbarIconKind::LineDashed:
      return L"\x865A\x7EBF";
    case ToolbarIconKind::LineDotted:
      return L"\x70B9\x7EBF";
    case ToolbarIconKind::Arrow:
      return L"\x7BAD\x5934";
    case ToolbarIconKind::Pen:
      return L"\x753B\x7B14";
    case ToolbarIconKind::Mosaic:
      return L"\x9A6C\x8D5B\x514B";
    case ToolbarIconKind::Text:
      return L"\x6587\x5B57";
    case ToolbarIconKind::Undo:
      return L"\x64A4\x9500";
    case ToolbarIconKind::Confirm:
      return L"\x5B8C\x6210";
    case ToolbarIconKind::Cancel:
      return L"\x53D6\x6D88";
    default:
      return L"";
  }
}

const wchar_t* toolbarStrokePresetLabel(int index)
{
  switch (index)
  {
    case 0:
      return L"\x7EC6";
    case 1:
      return L"\x4E2D";
    case 2:
      return L"\x7C97";
    default:
      return L"";
  }
}

const wchar_t* toolbarColorPresetLabel(int index)
{
  switch (index)
  {
    case 0:
      return L"\x7EA2";
    case 1:
      return L"\x6A59";
    case 2:
      return L"\x9EC4";
    case 3:
      return L"\x7EFF";
    case 4:
      return L"\x9752";
    case 5:
      return L"\x84DD";
    case 6:
      return L"\x7D2B";
    case 7:
      return L"\x767D";
    default:
      return L"";
  }
}

HWND createToolbarTooltip(HWND owner)
{
  if (owner == nullptr)
  {
    return nullptr;
  }

  INITCOMMONCONTROLSEX icc{};
  icc.dwSize = sizeof(icc);
  icc.dwICC = ICC_WIN95_CLASSES;
  (void)InitCommonControlsEx(&icc);

  const HWND tooltip = CreateWindowExW(
      WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
      WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT,
      CW_USEDEFAULT, CW_USEDEFAULT, owner, nullptr, GetModuleHandleW(nullptr),
      nullptr);
  if (tooltip == nullptr)
  {
    return nullptr;
  }

  SetWindowPos(tooltip, HWND_TOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  SendMessageW(tooltip, TTM_SETDELAYTIME, TTDT_INITIAL,
               DefaultModernToolbarMetrics.tooltip_delay_ms);
  SendMessageW(tooltip, TTM_SETMAXTIPWIDTH, 0, 240);
  return tooltip;
}

void bindToolbarTooltip(HWND tooltip, HWND owner, UINT id, const RECT& rect,
                        const wchar_t* text, wchar_t* storage,
                        std::size_t storage_chars)
{
  if (tooltip == nullptr || owner == nullptr || storage == nullptr ||
      storage_chars == 0)
  {
    return;
  }

  TOOLINFOW info{};
#ifdef TTTOOLINFOW_V2_SIZE
  info.cbSize = TTTOOLINFOW_V2_SIZE;
#else
  info.cbSize = sizeof(TOOLINFOW);
#endif
  info.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
  info.hwnd = owner;
  info.uId = static_cast<UINT_PTR>(id);
  info.rect = rect;
  info.lpszText = storage;

  const bool hidden = (rect.right <= rect.left) || (rect.bottom <= rect.top);
  if (hidden || text == nullptr || text[0] == L'\0')
  {
    storage[0] = L'\0';
    SendMessageW(tooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    return;
  }

  copyWide(storage, storage_chars, text);
  SendMessageW(tooltip, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&info));
  SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
}

int modernToolbarHeight(const ModernToolbarMetrics& metrics)
{
  return metrics.item_size + metrics.bar_padding * 2;
}

int modernToolbarWidth(int item_count, int extra_width,
                       const ModernToolbarMetrics& metrics)
{
  if (item_count < 0)
  {
    item_count = 0;
  }
  const int gaps = item_count > 0 ? item_count - 1 : 0;
  return metrics.bar_padding * 2 + item_count * metrics.item_size +
         gaps * metrics.gap + extra_width;
}

}  // namespace qingying
