#include "qingying/window/window_detector.hpp"

#include "window_query_helpers.h"

namespace qingying {

bool WindowDetector::snapshotAt(int x, int y, HWND& out_window,
                                WindowRect& out_rect) const noexcept {
  out_window = nullptr;
  out_rect = {};
  HWND window = GetTopWindow(nullptr);
  for (unsigned count = 0; window && count < 128;
       ++count, window = GetWindow(window, GW_HWNDNEXT)) {
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid || pid == GetCurrentProcessId() || window == GetShellWindow() ||
        window == GetDesktopWindow() || !IsWindowVisible(window) ||
        IsIconic(window)) continue;
    const auto style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    if (style & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) continue;
    RECT rect{};
    if (GetWindowRect(window, &rect) && x >= rect.left && x < rect.right &&
        y >= rect.top && y < rect.bottom) {
      out_window = window;
      out_rect = {rect.left, rect.top, rect.right, rect.bottom};
      return true;
    }
  }
  return false;
}

bool WindowDetector::isSnappable(HWND hwnd) {
  if (!window_detail::commonCandidate(hwnd)) return false;
  RECT rect{};
  return GetWindowRect(hwnd, &rect) != FALSE &&
      window_detail::intersectsVirtualDesktop(rect);
}

bool WindowDetector::matchesVisibleBounds(
    HWND hwnd, const WindowRect& expected) noexcept
{
  if (hwnd == nullptr || expected.empty() || IsWindow(hwnd) == FALSE)
  {
    return false;
  }
  RECT actual{};
  return window_detail::readVisibleBounds(hwnd, actual) &&
         actual.left == expected.left && actual.top == expected.top &&
         actual.right == expected.right && actual.bottom == expected.bottom;
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
    if (isSnappable(hwnd) ||
        window_detail::isBrowserOwnedTransientPopup(hwnd)) {
      RECT r{};
      if (window_detail::readVisibleBounds(hwnd, r) &&
          pt.x >= r.left && pt.x < r.right &&
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
