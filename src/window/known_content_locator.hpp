#pragma once

#include <Windows.h>

#include "qingying/window/smart_region_detector.hpp"

namespace qingying::window_detail {

bool locateKnownContent(HWND root_window, POINT screen_point,
                        SmartRegionCandidate& out) noexcept;

}  // namespace qingying::window_detail
