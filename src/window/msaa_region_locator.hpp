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

bool makeMsaaCandidate(HWND root_window, HWND target_window,
                       POINT screen_point,
                       const MsaaRegionProperties& properties,
                       SmartRegionCandidate& out) noexcept;

// 仅在 UIA 没有有效局部候选时调用；失败时保持 out 无效。
bool locateMsaaCandidate(HWND root_window, POINT screen_point,
                         SmartRegionCandidate& out) noexcept;

}  // namespace qingying::window_detail
