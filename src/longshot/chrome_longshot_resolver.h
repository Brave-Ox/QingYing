#pragma once

#include "qingying/longshot/longshot_profile.hpp"

#include <cstdint>

namespace qingying::longshot_detail {

// Resolves Chrome's visible renderer child. The returned content rectangle is
// the page viewport; browser chrome such as tabs, toolbar and scrollbars is
// intentionally excluded from long-shot capture.
bool resolveChromeTarget(std::uintptr_t owner_window,
                         LongShotProfileResult& out);

}  // namespace qingying::longshot_detail
