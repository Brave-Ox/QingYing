#include "qingying/app/tray_controller.hpp"

#include "qingying/app/app_messages.hpp"
#include "qingying/app/autostart_settings.hpp"
#include "qingying/app/tray_context_menu.hpp"
#include "qingying/app/tray_notify_icon.hpp"
#include "qingying/ui/modern_toolbar.hpp"

#include "resource.h"

#include <Shellapi.h>

#include <cwchar>

namespace qingying {
namespace {

constexpr wchar_t kWndClass[] = L"QingYing.TrayHiddenWindow";
constexpr int kCheckLeftOffsetPx = 6;
constexpr int kCheckDownOffsetPx = 3;
constexpr int kCheckRightOffsetPx = 7;
constexpr int kCheckUpOffsetPx = 6;
constexpr int kCheckPointCount = 3;
constexpr int kFallbackGlyphCount = 4;

static_assert(TrayMenuAutostartCommandId == IDM_TRAY_AUTOSTART);
static_assert(TrayMenuExitCommandId == IDM_TRAY_EXIT);

class UniqueGdiObject
{
 public:
  explicit UniqueGdiObject(HGDIOBJ obj)
      : m_obj(obj)
  {
  }

  ~UniqueGdiObject()
  {
    if (m_obj != nullptr)
    {
      DeleteObject(m_obj);
    }
  }

  UniqueGdiObject(const UniqueGdiObject&) = delete;
  UniqueGdiObject& operator=(const UniqueGdiObject&) = delete;

  HGDIOBJ get() const
  {
    return m_obj;
  }

 private:
  HGDIOBJ m_obj{nullptr};
};

int trayWindowDpi(HWND hwnd)
{
  if (hwnd == nullptr)
  {
    return TrayMenuDefaultDpi;
  }
  const UINT dpi = GetDpiForWindow(hwnd);
  if (dpi == 0)
  {
    return TrayMenuDefaultDpi;
  }
  return static_cast<int>(dpi);
}

void measureTrayMenuItem(HWND hwnd, MEASUREITEMSTRUCT* item)
{
  if (item == nullptr || item->CtlType != ODT_MENU)
  {
    return;
  }

  const int dpi = trayWindowDpi(hwnd);
  item->itemHeight =
      static_cast<UINT>(trayMenuMeasureHeight(item->itemData, dpi));

  const wchar_t* label = trayMenuLabelForKind(
      static_cast<TrayMenuItemKind>(item->itemData));
  int text_width =
      trayMenuScalePx(TrayMenuFontSizePx, dpi) * kFallbackGlyphCount;
  if (label[0] != L'\0' && hwnd != nullptr)
  {
    const HDC hdc = GetDC(hwnd);
    if (hdc != nullptr)
    {
      const LOGFONTW log_font = trayMenuLogFont(dpi);
      const UniqueGdiObject font(CreateFontIndirectW(&log_font));
      HGDIOBJ old_font = nullptr;
      if (font.get() != nullptr)
      {
        old_font = SelectObject(hdc, font.get());
      }
      SIZE size = {};
      const int count = static_cast<int>(std::wcslen(label));
      if (GetTextExtentPoint32W(hdc, label, count, &size) != FALSE)
      {
        text_width = size.cx;
      }
      if (old_font != nullptr)
      {
        SelectObject(hdc, old_font);
      }
      ReleaseDC(hwnd, hdc);
    }
  }

  item->itemWidth = static_cast<UINT>(
      trayMenuScalePx(TrayMenuCheckColumnPx + TrayMenuPadX, dpi) + text_width);
}

void drawTrayMenuCheck(HDC hdc, const RECT& item_rect, int dpi)
{
  const int column = trayMenuScalePx(TrayMenuCheckColumnPx, dpi);
  const int mid_x = item_rect.left + column / 2;
  const int mid_y = (item_rect.top + item_rect.bottom) / 2;
  POINT points[kCheckPointCount] = {};
  points[0].x = mid_x - trayMenuScalePx(kCheckLeftOffsetPx, dpi);
  points[0].y = mid_y;
  points[1].x = mid_x - trayMenuScalePx(kCheckDownOffsetPx, dpi);
  points[1].y = mid_y + trayMenuScalePx(kCheckDownOffsetPx, dpi);
  points[2].x = mid_x + trayMenuScalePx(kCheckRightOffsetPx, dpi);
  points[2].y = mid_y - trayMenuScalePx(kCheckUpOffsetPx, dpi);

  const UniqueGdiObject pen(CreatePen(
      PS_SOLID, trayMenuScalePx(TrayMenuCheckStrokePx, dpi), TrayMenuTextColor));
  if (pen.get() == nullptr)
  {
    return;
  }
  const HGDIOBJ old_pen = SelectObject(hdc, pen.get());
  const HGDIOBJ old_brush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
  if (Polyline(hdc, points, kCheckPointCount) == FALSE)
  {
    // 勾选勾画失败不影响菜单正文。
  }
  if (old_pen != nullptr)
  {
    SelectObject(hdc, old_pen);
  }
  if (old_brush != nullptr)
  {
    SelectObject(hdc, old_brush);
  }
}

void drawTrayMenuHoverCard(HDC hdc, const RECT& item_rect, int dpi)
{
  const RECT card = trayMenuHoverCardRect(item_rect, dpi);
  const int radius = trayMenuScalePx(TrayMenuHoverRadius, dpi);
  const int offset_x = trayMenuScalePx(TrayMenuHoverShadowOffsetX, dpi);
  const int offset_y = trayMenuScalePx(TrayMenuHoverShadowOffsetY, dpi);
  const int spread = trayMenuScalePx(TrayMenuHoverShadowSpreadPx, dpi);
  for (int layer = TrayMenuHoverShadowLayers; layer >= 1; --layer)
  {
    RECT shadow = card;
    OffsetRect(&shadow, offset_x * layer / TrayMenuHoverShadowLayers,
               offset_y * layer / TrayMenuHoverShadowLayers);
    InflateRect(&shadow, spread * (layer - 1), spread * (layer - 1));
    const COLORREF shade = trayMenuHoverShadowLayerColor(layer);
    fillRoundRect(hdc, shadow, shade, shade, radius);
  }
  fillRoundRect(hdc, card, TrayMenuHoverCardFill, TrayMenuHoverCardBorder,
                radius);
}

void drawTrayMenuItem(HWND hwnd, const DRAWITEMSTRUCT* item)
{
  if (item == nullptr || item->CtlType != ODT_MENU || item->hDC == nullptr)
  {
    return;
  }

  const int dpi = trayWindowDpi(hwnd);
  const RECT rc = item->rcItem;
  const bool selected = (item->itemState & ODS_SELECTED) != 0;
  const bool checked = (item->itemState & ODS_CHECKED) != 0;
  const TrayMenuItemKind kind = static_cast<TrayMenuItemKind>(item->itemData);
  const UniqueGdiObject fill_brush(CreateSolidBrush(TrayMenuFill));
  if (fill_brush.get() != nullptr)
  {
    FillRect(item->hDC, &rc, static_cast<HBRUSH>(fill_brush.get()));
  }

  if (kind == TrayMenuItemKind::Separator)
  {
    const UniqueGdiObject pen(CreatePen(
        PS_SOLID, trayMenuScalePx(TrayMenuSeparatorThicknessPx, dpi),
        TrayMenuSeparatorColor));
    if (pen.get() == nullptr)
    {
      return;
    }
    const HGDIOBJ old_pen = SelectObject(item->hDC, pen.get());
    const int y = (rc.top + rc.bottom) / 2;
    const int inset = trayMenuScalePx(TrayMenuSeparatorInsetPx, dpi);
    if (MoveToEx(item->hDC, rc.left + inset, y, nullptr) == FALSE ||
        LineTo(item->hDC, rc.right - inset, y) == FALSE)
    {
      // 分隔线绘制失败时该项仍可点。
    }
    if (old_pen != nullptr)
    {
      SelectObject(item->hDC, old_pen);
    }
    return;
  }

  if (selected)
  {
    drawTrayMenuHoverCard(item->hDC, rc, dpi);
  }

  if (checked)
  {
    drawTrayMenuCheck(item->hDC, rc, dpi);
  }

  const wchar_t* label = trayMenuLabelForKind(kind);
  if (label[0] == L'\0')
  {
    return;
  }

  const LOGFONTW log_font = trayMenuLogFont(dpi);
  const UniqueGdiObject font(CreateFontIndirectW(&log_font));
  HGDIOBJ old_font = nullptr;
  if (font.get() != nullptr)
  {
    old_font = SelectObject(item->hDC, font.get());
  }
  SetBkMode(item->hDC, TRANSPARENT);
  SetTextColor(item->hDC, TrayMenuTextColor);
  RECT text_rc = rc;
  text_rc.left += trayMenuScalePx(TrayMenuCheckColumnPx, dpi);
  if (DrawTextW(item->hDC, label, -1, &text_rc,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX) == 0)
  {
    // 无可见文字时仍保留点击区域。
  }
  if (old_font != nullptr)
  {
    SelectObject(item->hDC, old_font);
  }
}

}  // namespace

TrayController::TrayController() = default;

TrayController::~TrayController() {
  destroy();
}

bool TrayController::create(HINSTANCE instance) {
  instance_ = instance;
  taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &TrayController::WndProc;
  wc.hInstance = instance_;
  wc.lpszClassName = kWndClass;
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW

  if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  hwnd_ = CreateWindowExW(0, kWndClass, L"QingYing", WS_OVERLAPPED, 0, 0, 0, 0,
                          nullptr, nullptr, instance_, this);
  if (hwnd_ == nullptr) {
    return false;
  }

  icon_ = loadAppIcon(instance_);
  if (icon_ == nullptr) {
    return false;
  }
  return addTrayIcon();
}

void TrayController::destroy() {
  removeTrayIcon();
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (icon_ != nullptr) {
    DestroyIcon(icon_);
    icon_ = nullptr;
  }
}

void TrayController::setMessageFilter(MessageFilter filter) {
  message_filter_ = std::move(filter);
}

HICON TrayController::loadAppIcon(HINSTANCE instance) {
  HICON icon = static_cast<HICON>(LoadImageW(
      instance, MAKEINTRESOURCEW(IDI_QINGYING), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
      LR_DEFAULTCOLOR));
  if (icon == nullptr) {
    icon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));  // IDI_APPLICATION
  }
  return icon;
}

bool TrayController::addTrayIcon() {
  if (hwnd_ == nullptr) {
    return false;
  }

  NOTIFYICONDATAW nid = {};
  fillTrayNotifyIconData(nid, hwnd_, WM_QINGYING_TRAY, icon_);

  icon_added_ = Shell_NotifyIconW(NIM_ADD, &nid) == TRUE;
  if (icon_added_) {
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
  }
  return icon_added_;
}

void TrayController::removeTrayIcon() {
  if (!icon_added_ || hwnd_ == nullptr) {
    return;
  }
  NOTIFYICONDATAW nid = {};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd_;
  nid.uID = TrayIconId;
  Shell_NotifyIconW(NIM_DELETE, &nid);
  icon_added_ = false;
}

LRESULT CALLBACK TrayController::WndProc(HWND hwnd, UINT msg, WPARAM wparam,
                                         LPARAM lparam) {
  TrayController* self = nullptr;
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<TrayController*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  } else {
    self = reinterpret_cast<TrayController*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }

  if (self != nullptr) {
    return self->handleMessage(msg, wparam, lparam);
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void TrayController::showContextMenu() {
  POINT pt = {};
  GetCursorPos(&pt);

  HMENU menu = CreatePopupMenu();
  if (menu == nullptr) {
    return;
  }

  const bool autostart = AutostartSettings::isEnabled();
  MENUITEMINFOW autostart_item = {};
  fillTrayMenuItem(autostart_item, IDM_TRAY_AUTOSTART, autostart);
  MENUITEMINFOW separator = {};
  fillTrayMenuSeparator(separator);
  MENUITEMINFOW exit_item = {};
  fillTrayMenuItem(exit_item, IDM_TRAY_EXIT, false);
  if (InsertMenuItemW(menu, 0, TRUE, &autostart_item) == FALSE ||
      InsertMenuItemW(menu, 1, TRUE, &separator) == FALSE ||
      InsertMenuItemW(menu, 2, TRUE, &exit_item) == FALSE)
  {
    DestroyMenu(menu);
    return;
  }

  UniqueGdiObject background(CreateSolidBrush(TrayMenuFill));
  if (background.get() != nullptr)
  {
    MENUINFO menu_info = {};
    menu_info.cbSize = sizeof(menu_info);
    menu_info.fMask = MIM_STYLE | MIM_BACKGROUND;
    menu_info.dwStyle = MNS_NOCHECK;
    menu_info.hbrBack = static_cast<HBRUSH>(background.get());
    // 失败时仍弹出菜单，系统默认底亦可读。
    SetMenuInfo(menu, &menu_info);
  }

  // Required so menu dismisses correctly when clicking elsewhere.
  SetForegroundWindow(hwnd_);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x,
                 pt.y, 0, hwnd_, nullptr);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

LRESULT TrayController::handleMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
  if (message_filter_) {
    LRESULT filtered = 0;
    if (message_filter_(msg, wparam, lparam, &filtered)) {
      return filtered;
    }
  }

  if (taskbar_created_msg_ != 0 && msg == taskbar_created_msg_) {
    icon_added_ = false;
    addTrayIcon();
    return 0;
  }

  switch (msg) {
    case WM_QINGYING_TRAY: {
      const UINT mouse = LOWORD(lparam);
      if (mouse == WM_RBUTTONUP || mouse == WM_CONTEXTMENU) {
        showContextMenu();
      }
      return 0;
    }

    case WM_CONTEXTMENU:
      // NOTIFYICON_VERSION_4 may deliver this instead of callback mouse msg.
      showContextMenu();
      return 0;

    case WM_MEASUREITEM:
      measureTrayMenuItem(hwnd_, reinterpret_cast<MEASUREITEMSTRUCT*>(lparam));
      return TRUE;

    case WM_DRAWITEM:
      drawTrayMenuItem(hwnd_, reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
      return TRUE;

    case WM_COMMAND:
      onCommand(LOWORD(wparam));
      return 0;

    case WM_DESTROY:
      removeTrayIcon();
      PostQuitMessage(0);
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hwnd_, msg, wparam, lparam);
}

void TrayController::onCommand(UINT id) {
  switch (id) {
    case IDM_TRAY_AUTOSTART: {
      const bool next = !AutostartSettings::isEnabled();
      AutostartSettings::setEnabled(next);
      break;
    }
    case IDM_TRAY_EXIT:
      DestroyWindow(hwnd_);
      break;
    default:
      break;
  }
}

}  // namespace qingying
