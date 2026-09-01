#include "qingying/longshot/longshot_profile_registry.hpp"

#include "qingying/longshot/explorer_longshot_profile.hpp"
#include "qingying/longshot/notepad_longshot_profile.hpp"

#include <utility>

namespace qingying {

LongShotProfileRegistry::LongShotProfileRegistry() {
  profiles_.push_back(std::make_unique<NotepadLongShotProfile>());
  profiles_.push_back(std::make_unique<ExplorerLongShotProfile>());
}

LongShotProfileRegistry::LongShotProfileRegistry(
    std::vector<std::unique_ptr<LongShotProfile>> profiles)
    : profiles_(std::move(profiles)) {}

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
