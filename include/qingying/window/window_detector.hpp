#pragma once

#include <Windows.h>

namespace qingying {

// 窗口矩形（屏幕 / 虚拟桌面坐标，right/bottom 为开区间）。
struct WindowRect {
  int left{0};
  int top{0};
  int right{0};
  int bottom{0};

  int width() const { return right - left; }
  int height() const { return bottom - top; }
  bool empty() const { return width() <= 0 || height() <= 0; }
};

// 根据鼠标屏幕坐标识别可吸附的可见顶层窗口。
// 负责窗口发现与候选过滤，不散落到选区状态机中。
class WindowDetector {
 public:
  // UI fallback: bounded Z-order walk using window-manager metadata only.
  bool snapshotAt(int screen_x, int screen_y, HWND& out_window,
                  WindowRect& out_rect) const noexcept;
  // 判断窗口是否可吸附（供测试与复用）。
  static bool isSnappable(HWND hwnd);
  // 仅比较当前可见边界；用于短时窗口快照缓存的失效校验。
  static bool matchesVisibleBounds(HWND hwnd,
                                   const WindowRect& expected) noexcept;

  // 返回 false 表示没有可吸附窗口（如悬停在桌面或 QingYing 自身窗口上）。
  bool detectAt(int screen_x, int screen_y, HWND& out_window,
                WindowRect& out_rect) const;
};

}  // namespace qingying
