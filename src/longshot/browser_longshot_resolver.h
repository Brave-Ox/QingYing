#pragma once

#include "qingying/longshot/longshot_profile.hpp"

#include <cstdint>

namespace qingying::longshot_detail {

// Resolves a Chromium browser window to the child that receives page
// scrolling. The returned renderer viewport excludes browser chrome such as
// tabs and the address bar. Other browser families require later adapters.
bool resolveBrowserTarget(std::uintptr_t owner_window,
                          const LongShotRequest& request,
                          LongShotProfileResult& out);

}  // namespace qingying::longshot_detail
