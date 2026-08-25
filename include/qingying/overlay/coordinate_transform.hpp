#pragma once

namespace qingying {
namespace coord {

// 虚拟桌面（所有显示器并集）边界。
// 坐标与当前进程的 DPI 感知保持一致：DPI-aware 进程为物理像素，
// DPI-unaware 进程为虚拟化逻辑像素。覆盖层与捕获引擎使用同一坐标系，
// 因此任意缩放比下都不会产生选框与截图内容的偏移。
struct VirtualScreenRect {
  int left{0};
  int top{0};
  int width{0};
  int height{0};

  int right() const { return left + width; }
  int bottom() const { return top + height; }
};

// 虚拟桌面（所有显示器并集）边界（Win32 封装）。
VirtualScreenRect getVirtualScreen();

// 系统 DPI（GetDeviceCaps LOGPIXELSX 封装）；失败返回 96。
int getSystemDpi();

// 逻辑坐标 ↔ 物理像素转换。dpi == 96 时恒等；125% → 120，150% → 144。
inline int logicalToPhysical(int value, int dpi) {
  return value * dpi / 96;
}

inline int physicalToLogical(int value, int dpi) {
  return dpi == 0 ? 0 : value * 96 / dpi;
}

// 覆盖层客户区坐标（原点 0,0）→ 虚拟桌面屏幕坐标（加虚拟桌面原点偏移）。
inline int clientToScreenX(int client_x, const VirtualScreenRect& screen) {
  return client_x + screen.left;
}

inline int clientToScreenY(int client_y, const VirtualScreenRect& screen) {
  return client_y + screen.top;
}

}  // namespace coord
}  // namespace qingying
