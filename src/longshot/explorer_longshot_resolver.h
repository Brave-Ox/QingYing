#pragma once

#include "qingying/longshot/longshot_profile.hpp"

namespace qingying::longshot_detail {

bool resolveExplorerTarget(const LongShotRequest& request,
                           LongShotProfileResult& out);

}  // namespace qingying::longshot_detail
