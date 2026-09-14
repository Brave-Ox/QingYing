#pragma once

#include "qingying/longshot/longshot_plugin_host.h"
#include "qingying/longshot/longshot_profile.hpp"
#include "qingying/longshot/longshot_profile_registry.hpp"

#include <cstddef>
#include <memory>

namespace qingying {

// Adapts one validated DLL descriptor to the native LongShotProfile interface.
// The LoadedPlugin and its host must outlive this profile. The host must not
// unload the plugin while any DllLongShotProfile is still in use.
class DllLongShotProfile final : public LongShotProfile {
 public:
  explicit DllLongShotProfile(
      const LongShotPluginHost::LoadedPlugin& plugin);
  ~DllLongShotProfile() override;

  DllLongShotProfile(const DllLongShotProfile&) = delete;
  DllLongShotProfile& operator=(const DllLongShotProfile&) = delete;

  const char* name() const noexcept override;

  bool resolve(const LongShotRequest& request,
               LongShotProfileResult& out) const override;
  bool scrollDown(const LongShotRequest& request,
                  const LongShotProfileResult& profile) const override;
  bool queryScrollState(const LongShotProfileResult& profile,
                        LongShotScrollState& out) const override;
  void cancel() const noexcept override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Adds one bridge profile for every plugin currently loaded by host. The
// registry does not take ownership of host; host must remain alive until the
// registry and all engines using these profiles have been destroyed.
std::size_t addDllLongShotProfiles(const LongShotPluginHost& host,
                                   LongShotProfileRegistry& registry);

}  // namespace qingying
