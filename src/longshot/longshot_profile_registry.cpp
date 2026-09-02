#include "qingying/longshot/longshot_profile_registry.hpp"

#include <utility>

namespace qingying {

LongShotProfileRegistry::LongShotProfileRegistry(
    std::vector<std::unique_ptr<LongShotProfile>> profiles)
    : profiles_(std::move(profiles)) {}

void LongShotProfileRegistry::add(
    std::unique_ptr<LongShotProfile> profile) {
  if (profile != nullptr) {
    profiles_.push_back(std::move(profile));
  }
}

const LongShotProfile* LongShotProfileRegistry::resolve(
    const LongShotRequest& request, LongShotProfileResult& out) const {
  out = LongShotProfileResult{};
  for (const auto& profile : profiles_) {
    if (profile != nullptr && profile->resolve(request, out)) {
      return profile.get();
    }
  }
  out = LongShotProfileResult{};
  return nullptr;
}

}  // qingying 命名空间
