#pragma once

#include "qingying/longshot/longshot_profile.hpp"

#include <cstdint>

namespace qingying {
namespace longshot_detail {

// A profile miss means that no application adapter claimed the request.  The
// generic resolver reports a separate, stable reason when using the recorded
// owner as a fallback would be unsafe.
enum class GenericScrollTargetStatus : std::uint8_t {
  Resolved,
  InvalidRequest,
  OwnerUnavailable,
  SelfUi,
  SelectionOutsideOwner,
  HitTestFailed,
  CrossPane,
  TargetUnavailable,
};

struct GenericScrollTargetPolicy {
  // Zero disables process exclusion for controlled hosts and tests. Production
  // callers use the overload that excludes the current QingYing process.
  std::uint32_t excluded_process_id{0};
};

struct GenericScrollTarget {
  std::uintptr_t owner_window{0};
  std::uintptr_t target_window{0};
  std::uint32_t owner_process_id{0};
  std::uint32_t target_process_id{0};
  int anchor_x{0};
  int anchor_y{0};
  ScreenPhysicalRect content;

  bool valid() const noexcept;
};

struct GenericScrollTargetResolution {
  GenericScrollTargetStatus status{GenericScrollTargetStatus::InvalidRequest};
  GenericScrollTarget target;

  bool resolved() const noexcept {
    return status == GenericScrollTargetStatus::Resolved && target.valid();
  }
};

GenericScrollTargetResolution resolveGenericScrollTarget(
    const LongShotRequest& request);
GenericScrollTargetResolution resolveGenericScrollTarget(
    const LongShotRequest& request, GenericScrollTargetPolicy policy);

// Re-runs owner-scoped hit testing and compares window identity, anchor and
// physical content bounds. A moved, replaced or destroyed target is stale.
bool validateGenericScrollTarget(
    const LongShotRequest& request, const GenericScrollTarget& target);
bool validateGenericScrollTarget(
    const LongShotRequest& request, const GenericScrollTarget& target,
    GenericScrollTargetPolicy policy);

}  // namespace longshot_detail
}  // namespace qingying
