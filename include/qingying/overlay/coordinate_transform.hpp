#pragma once

#include "qingying/geometry/rect_types.h"

namespace qingying {
namespace coord {

// 虚拟桌面（所有显示器并集）边界。
// 坐标与当前进程的 DPI 感知保持一致：DPI-aware 进程为物理像素，
// DPI-unaware 进程为虚拟化逻辑像素。覆盖层与捕获引擎使用同一坐标系，
// 因此任意缩放比下都不会产生选框与截图内容的偏移。
struct VirtualScreenRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  int right() const { return x + width; }
  int bottom() const { return y + height; }
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
  return client_x + screen.x;
}

inline int clientToScreenY(int client_y, const VirtualScreenRect& screen) {
  return client_y + screen.y;
}

// 虚拟桌面屏幕坐标 → 覆盖层客户区坐标（减虚拟桌面原点偏移）。
inline int screenToClientX(int screen_x, const VirtualScreenRect& screen) {
  return screen_x - screen.x;
}

inline int screenToClientY(int screen_y, const VirtualScreenRect& screen) {
  return screen_y - screen.y;
}

// Explicit rectangle conversions. The distinct return types make the
// coordinate space visible at the call site instead of relying on comments
// beside four unrelated integers.
inline ScreenPhysicalRect clientToScreen(const OverlayClientRect& client,
                                         const VirtualScreenRect& screen) {
  return ScreenPhysicalRect{clientToScreenX(client.x, screen),
                            clientToScreenY(client.y, screen), client.width,
                            client.height};
}

inline OverlayClientRect screenToClient(
    const ScreenPhysicalRect& screen_rect,
    const VirtualScreenRect& screen) {
  return OverlayClientRect{screenToClientX(screen_rect.x, screen),
                           screenToClientY(screen_rect.y, screen),
                           screen_rect.width, screen_rect.height};
}

// A desktop screenshot is indexed from the virtual-screen origin. This
// conversion is used when a physical selection is pasted into that Image.
inline ImagePixelRect screenToImage(
    const ScreenPhysicalRect& screen_rect,
    const VirtualScreenRect& image_screen_rect) {
  return ImagePixelRect{screen_rect.x - image_screen_rect.x,
                        screen_rect.y - image_screen_rect.y,
                        screen_rect.width, screen_rect.height};
}

inline ScreenPhysicalRect imageToScreen(
    const ImagePixelRect& image_rect,
    const VirtualScreenRect& image_screen_rect) {
  return ScreenPhysicalRect{image_rect.x + image_screen_rect.x,
                            image_rect.y + image_screen_rect.y,
                            image_rect.width, image_rect.height};
}

}  // namespace coord
}  // namespace qingying
