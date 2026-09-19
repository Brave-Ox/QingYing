#pragma once

#include "generic_scroll_target_resolver.hpp"

#include <atomic>
#include <cstdint>

namespace qingying {
namespace longshot_detail {

struct GenericWheelProfilePolicy {
  GenericScrollTargetPolicy target_policy;
  std::uint32_t trusted_ui_process_id{0};
  bool enforce_foreground{true};
  int max_inputs{64};
};

bool genericWheelForegroundIsSafe(
    const GenericScrollTarget& target, std::uint32_t trusted_ui_process_id,
    std::uintptr_t foreground_window) noexcept;

class GenericWheelLongShotProfile final : public LongShotProfile {
 public:
  GenericWheelLongShotProfile();
  explicit GenericWheelLongShotProfile(GenericWheelProfilePolicy policy);

  const char* name() const noexcept override;
  bool resolve(const LongShotRequest& request,
               LongShotProfileResult& out) const override;
  bool scrollDown(const LongShotRequest& request,
                  const LongShotProfileResult& profile) const override;
  bool queryScrollState(const LongShotProfileResult& profile,
                        LongShotScrollState& out) const override;
  void cancel() const noexcept override;

  GenericScrollTargetStatus lastResolutionStatus() const noexcept;
  int inputCount() const noexcept;

 private:
  bool sameRequest(const LongShotRequest& request) const noexcept;
  bool foregroundIsSafe() const noexcept;
  void clear() const noexcept;

  GenericWheelProfilePolicy policy_;
  mutable LongShotRequest request_;
  mutable GenericScrollTarget target_;
  mutable GenericScrollTargetStatus status_{
      GenericScrollTargetStatus::InvalidRequest};
  mutable int input_count_{0};
  mutable std::atomic_bool cancelled_{false};
};

}  // namespace longshot_detail
}  // namespace qingying
