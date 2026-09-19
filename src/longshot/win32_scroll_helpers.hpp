#pragma once

#include "qingying/longshot/longshot_profile.hpp"

#include <cstdint>

namespace qingying {
namespace longshot_detail {

bool sendWheelDown(const LongShotRequest& request,
                   const LongShotProfileResult& profile);

bool queryVerticalScrollState(const LongShotProfileResult& profile,
                              LongShotScrollState& out);

bool windowBelongsToOwner(std::uintptr_t owner_window,
                          std::uintptr_t target_window) noexcept;
bool windowClientScreenRect(std::uintptr_t window,
                            ScreenPhysicalRect& out) noexcept;
std::uint32_t windowProcessId(std::uintptr_t window) noexcept;

}  // longshot_detail 命名空间
}  // qingying 命名空间
