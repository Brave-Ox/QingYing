#include "qingying/app/tray_controller.hpp"

#include "qingying/app/autostart_settings.hpp"

#include "resource.h"

#include <Shellapi.h>

namespace qingying {
namespace {

constexpr wchar_t kWndClass[] = L"QingYing.TrayHiddenWindow";
constexpr UINT WM_QINGYING_TRAY = WM_APP + 1;

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
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd_;
  nid.uID = 1;
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.uCallbackMessage = WM_QINGYING_TRAY;
  nid.hIcon = icon_;
  wcsncpy_s(nid.szTip, L"QingYing", _TRUNCATE);

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
  nid.uID = 1;
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
  AppendMenuW(menu, MF_STRING | (autostart ? MF_CHECKED : MF_UNCHECKED),
              IDM_TRAY_AUTOSTART, L"Start with Windows");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT, L"Exit");

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
