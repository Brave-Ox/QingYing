#include "qingying/longshot/explorer_longshot_profile.hpp"

#include "explorer_longshot_resolver.h"
#include "win32_scroll_helpers.hpp"

namespace qingying {

bool resolveExplorerProfile(const LongShotRequest& request,
                            LongShotProfileResult& out) {
  return longshot_detail::resolveExplorerTarget(request, out);
}

bool ExplorerLongShotProfile::resolve(const LongShotRequest& request,
                                      LongShotProfileResult& out) const {
  return resolveExplorerProfile(request, out);
}

bool ExplorerLongShotProfile::scrollDown(
    const LongShotRequest& request,
    const LongShotProfileResult& profile) const {
  return longshot_detail::sendWheelDown(request, profile);
}

bool ExplorerLongShotProfile::queryScrollState(
    const LongShotProfileResult& profile, LongShotScrollState& out) const {
  return longshot_detail::queryVerticalScrollState(profile, out);
}

}  // namespace qingying
