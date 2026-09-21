#pragma once

#include "qingying/longshot/longshot_profile.hpp"

#include <cstdint>

namespace qingying {
namespace longshot_detail {

bool sendWheelDown(const LongShotRequest& request,
                   const LongShotProfileResult& profile);

bool queryVerticalScrollState(const LongShotProfileResult& profile,
                              LongShotScrollState& out);

// Sends exactly one wheel message. A timeout is a failure and is never
// followed by a queued duplicate input.
bool sendBoundedWheelDown(std::uintptr_t owner_window,
                          std::uintptr_t target_window, int screen_x,
                          int screen_y) noexcept;

bool windowBelongsToOwner(std::uintptr_t owner_window,
                          std::uintptr_t target_window) noexcept;
bool windowClientScreenRect(std::uintptr_t window,
                            ScreenPhysicalRect& out) noexcept;
std::uint32_t windowProcessId(std::uintptr_t window) noexcept;

}  // longshot_detail 命名空间
}  // qingying 命名空间
