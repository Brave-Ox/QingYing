#include "qingying/window/window_detector.hpp"

#include <dwmapi.h>

#include <cwchar>

namespace qingying {

namespace {

bool isOwnProcess(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  return pid == GetCurrentProcessId();
}

bool isToolWindow(HWND hwnd) {
  const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  return (ex_style & WS_EX_TOOLWINDOW) != 0;
}

bool isCloaked(HWND hwnd) {
  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked,
                                      sizeof(cloaked)))) {
    return cloaked != FALSE;
  }
  return false;
}

bool isDesktopShell(HWND hwnd) {
  if (hwnd == GetDesktopWindow()) {
    return true;  // 桌面窗口本身
  }
  wchar_t cls[64] = {};
  GetClassNameW(hwnd, cls, 64);
  return std::wcscmp(cls, L"Progman") == 0 || std::wcscmp(cls, L"WorkerW") == 0 ||
         std::wcscmp(cls, L"#32769") == 0;
}

}  // namespace

bool WindowDetector::isSnappable(HWND hwnd) {
  if (hwnd == nullptr || !IsWindow(hwnd)) {
    return false;
  }
  // QingYing 自身窗口：覆盖层 / 操作条 / 托盘 / 钉图。
  if (isOwnProcess(hwnd)) {
    return false;
  }
  if (!IsWindowVisible(hwnd)) {
    return false;
  }
  if (IsIconic(hwnd)) {
    return false;  // 最小化窗口
  }
  if (isToolWindow(hwnd)) {
    return false;  // 工具窗口
  }
  if (isCloaked(hwnd)) {
    return false;  // UWP 隐藏窗口
  }
  if (isDesktopShell(hwnd)) {
    return false;  // 桌面外壳（Progman / WorkerW / 桌面窗口）
  }
  // 过滤完全位于虚拟桌面之外的窗口（常见于藏在负坐标的隐藏窗口）。
  RECT rect{};
  if (!GetWindowRect(hwnd, &rect)) {
    return false;
  }
  const int virt_left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int virt_top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int virt_right = virt_left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int virt_bottom = virt_top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
  if (rect.right <= virt_left || rect.bottom <= virt_top ||
      rect.left >= virt_right || rect.top >= virt_bottom) {
    return false;
  }
  return true;
}

bool WindowDetector::detectAt(int screen_x, int screen_y, HWND& out_window,
                              WindowRect& out_rect) const {
  const POINT pt{screen_x, screen_y};

  HWND hwnd = WindowFromPoint(pt);
  if (hwnd != nullptr) {
    hwnd = GetAncestor(hwnd, GA_ROOT);
  }

  // 覆盖层是全屏置顶窗口，WindowFromPoint 会命中它；沿 Z 序向下跳过
  // QingYing 自身窗口，找到首个可吸附且包含该点的顶层窗口。
  while (hwnd != nullptr) {
    hwnd = GetAncestor(hwnd, GA_ROOT);
    if (isSnappable(hwnd)) {
      RECT r{};
      if (GetWindowRect(hwnd, &r) && pt.x >= r.left && pt.x < r.right &&
          pt.y >= r.top && pt.y < r.bottom) {
        out_window = hwnd;
        out_rect = {r.left, r.top, r.right, r.bottom};
        return true;
      }
    }
    hwnd = GetWindow(hwnd, GW_HWNDNEXT);
  }
  return false;
}

}  // namespace qingying
