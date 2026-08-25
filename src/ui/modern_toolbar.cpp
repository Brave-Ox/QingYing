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

void drawToolbarItem(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     bool hovered, bool selected, bool enabled, bool accent)
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
    case ToolbarIconKind::Arrow:
      return L"\x7BAD\x5934";
    case ToolbarIconKind::Pen:
      return L"\x753B\x7B14";
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

  copyWide(storage, storage_chars, text);

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
