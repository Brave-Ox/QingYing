#include "qingying/app/command_window.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace qingying {
namespace {

constexpr wchar_t kCommandWindowClass[] = L"QingYing.CommandWindow";
constexpr wchar_t kCommandWindowTitle[] = L"QingYing 口令";
constexpr int kExecuteButtonId = 1;
constexpr int kCloseButtonId = 2;
constexpr int kControlMargin = 16;
constexpr int kControlGap = 10;
constexpr int kButtonWidth = 84;
constexpr int kButtonHeight = 30;
constexpr int kStatusHeight = 44;
constexpr int kDefaultWidth = 500;
constexpr int kDefaultHeight = 190;
constexpr int kMaximumCommandChars = 512;

void centerOverOwner(HWND hwnd, HWND owner) noexcept {
  RECT target{};
  if (owner == nullptr || GetWindowRect(owner, &target) == FALSE ||
      target.right <= target.left || target.bottom <= target.top) {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &target, 0);
  }
  RECT window{};
  GetWindowRect(hwnd, &window);
  const int width = window.right - window.left;
  const int height = window.bottom - window.top;
  const int x = target.left + ((target.right - target.left) - width) / 2;
  const int y = target.top + ((target.bottom - target.top) - height) / 2;
  SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOACTIVATE);
}

}  // namespace

CommandWindow::~CommandWindow() {
  close();
}

bool CommandWindow::show(HWND owner, ExecuteCallback execute) {
  execute_ = std::move(execute);
  if (hwnd_ == nullptr && !create(owner)) return false;
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  SetForegroundWindow(hwnd_);
  SetFocus(edit_);
  return true;
}

void CommandWindow::close() noexcept {
  if (hwnd_ != nullptr) DestroyWindow(hwnd_);
  hwnd_ = nullptr;
  edit_ = nullptr;
  status_ = nullptr;
  execute_ = {};
}

bool CommandWindow::visible() const noexcept {
  return hwnd_ != nullptr && IsWindowVisible(hwnd_) != FALSE;
}

bool CommandWindow::create(HWND owner) {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &CommandWindow::WndProc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = kCommandWindowClass;
  if (RegisterClassExW(&window_class) == 0 &&
      GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kCommandWindowClass,
                          kCommandWindowTitle,
                          WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                              WS_MINIMIZEBOX,
                          CW_USEDEFAULT, CW_USEDEFAULT, kDefaultWidth,
                          kDefaultHeight, owner, nullptr,
                          GetModuleHandleW(nullptr), this);
  if (hwnd_ == nullptr) return false;

  edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                          0, 0, 0, 0, hwnd_, nullptr,
                          GetModuleHandleW(nullptr), nullptr);
  status_ = CreateWindowExW(0, L"STATIC",
                            L"示例：截取微信窗口并复制",
                            WS_CHILD | WS_VISIBLE | SS_LEFT,
                            0, 0, 0, 0, hwnd_, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
  const HWND execute_button = CreateWindowExW(
      0, L"BUTTON", L"执行", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
      0, 0, 0, 0, hwnd_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExecuteButtonId)),
      GetModuleHandleW(nullptr), nullptr);
  const HWND close_button = CreateWindowExW(
      0, L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
      0, 0, 0, 0, hwnd_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCloseButtonId)),
      GetModuleHandleW(nullptr), nullptr);
  if (edit_ == nullptr || status_ == nullptr || execute_button == nullptr ||
      close_button == nullptr) {
    close();
    return false;
  }

  const HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  for (HWND child : {edit_, status_, execute_button, close_button}) {
    SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  }
  SendMessageW(edit_, EM_SETLIMITTEXT, kMaximumCommandChars, 0);
  SetWindowTextW(edit_, L"截取微信窗口并复制");
  centerOverOwner(hwnd_, owner);
  return true;
}

void CommandWindow::layout(int width, int height) noexcept {
  if (width <= 0 || height <= 0) return;
  const int content_width = (std::max)(1, width - kControlMargin * 2);
  const int edit_y = kControlMargin + 22;
  const int edit_height = 28;
  const int button_y = edit_y + edit_height + kControlGap;
  const int status_y = button_y + kButtonHeight + kControlGap;
  MoveWindow(edit_, kControlMargin, edit_y, content_width, edit_height, TRUE);
  MoveWindow(GetDlgItem(hwnd_, kExecuteButtonId),
             width - kControlMargin - kButtonWidth * 2 - kControlGap,
             button_y, kButtonWidth, kButtonHeight, TRUE);
  MoveWindow(GetDlgItem(hwnd_, kCloseButtonId),
             width - kControlMargin - kButtonWidth, button_y,
             kButtonWidth, kButtonHeight, TRUE);
  MoveWindow(status_, kControlMargin, status_y, content_width,
             (std::max)(1, height - status_y - kControlMargin), TRUE);
}

void CommandWindow::executeCurrentCommand() {
  if (edit_ == nullptr || status_ == nullptr) return;
  const int length = GetWindowTextLengthW(edit_);
  if (length <= 0) {
    SetWindowTextW(status_, L"请输入口令。");
    return;
  }
  std::wstring command(static_cast<std::size_t>(length) + 1, L'\0');
  GetWindowTextW(edit_, command.data(), static_cast<int>(command.size()));
  command.resize(static_cast<std::size_t>(length));
  const std::wstring feedback = execute_ ? execute_(command)
                                         : L"口令服务当前不可用。";
  SetWindowTextW(status_, feedback.c_str());
}

LRESULT CALLBACK CommandWindow::WndProc(HWND hwnd, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
  CommandWindow* self = reinterpret_cast<CommandWindow*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    self = static_cast<CommandWindow*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(self));
    if (self != nullptr) self->hwnd_ = hwnd;
  }
  if (self == nullptr) return DefWindowProcW(hwnd, message, wparam, lparam);
  return self->handleMessage(message, wparam, lparam);
}

LRESULT CommandWindow::handleMessage(UINT message, WPARAM wparam,
                                     LPARAM lparam) {
  switch (message) {
    case WM_SIZE:
      layout(LOWORD(lparam), HIWORD(lparam));
      return 0;
    case WM_COMMAND:
      if (LOWORD(wparam) == kExecuteButtonId) {
        executeCurrentCommand();
        return 0;
      }
      if (LOWORD(wparam) == kCloseButtonId) {
        ShowWindow(hwnd_, SW_HIDE);
        return 0;
      }
      break;
    case WM_CLOSE:
      ShowWindow(hwnd_, SW_HIDE);
      return 0;
    case WM_DESTROY:
      hwnd_ = nullptr;
      edit_ = nullptr;
      status_ = nullptr;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd_, message, wparam, lparam);
}

}  // namespace qingying
