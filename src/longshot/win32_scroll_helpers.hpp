#pragma once

#include "qingying/longshot/longshot_profile.hpp"

namespace qingying {
namespace longshot_detail {

bool sendWheelDown(const LongShotRequest& request,
                   const LongShotProfileResult& profile);

bool queryVerticalScrollState(const LongShotProfileResult& profile,
                              LongShotScrollState& out);

}  // longshot_detail 命名空间
}  // qingying 命名空间
