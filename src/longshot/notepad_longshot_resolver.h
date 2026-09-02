#pragma once

#include "qingying/longshot/longshot_profile.hpp"

namespace qingying::longshot_detail {

bool resolveNotepadTarget(std::uintptr_t owner_window,
                          LongShotProfileResult& out);

}  // namespace qingying::longshot_detail
