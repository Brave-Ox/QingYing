#pragma once

#include <cstdint>

#include <Windows.h>
#include <oleacc.h>

#include "qingying/window/smart_region_detector.hpp"

namespace qingying::window_detail {

struct MsaaRegionProperties
{
  WindowRect rect;
  LONG role{0};
  LONG state{0};
};

// MSAA 的 accLocation 使用客户区屏幕坐标；用于拒绝覆盖整个根客户区的
// ContentSurface，避免浏览器窗口外壳进入异步候选。
bool msaaRectCoversRootClientArea(const WindowRect& rect,
                                  const WindowRect& root_client_rect) noexcept;

bool makeMsaaCandidate(HWND root_window, HWND target_window,
                       POINT screen_point,
                       const MsaaRegionProperties& properties,
                       SmartRegionCandidate& out,
                       std::uint8_t accessibility_depth = 0) noexcept;

// 仅在 UIA 没有有效局部候选时调用；失败时保持 out 无效。
bool locateMsaaCandidate(HWND root_window, POINT screen_point,
                         SmartRegionCandidate& out) noexcept;

}  // namespace qingying::window_detail
