#include "generic_wheel_longshot_profile.hpp"

#include "win32_scroll_helpers.hpp"

#include <Windows.h>

namespace qingying {
namespace longshot_detail {
namespace {

bool sameRect(const ScreenPhysicalRect& left,
              const ScreenPhysicalRect& right) noexcept {
  return left == right;
}

}  // namespace

bool genericWheelForegroundIsSafe(
    const GenericScrollTarget& target, std::uint32_t trusted_ui_process_id,
    std::uintptr_t foreground_window) noexcept {
  if (!target.valid() || foreground_window == 0) return false;
  const std::uint32_t foreground_process =
      windowProcessId(foreground_window);
  if (foreground_process == target.owner_process_id) {
    return windowBelongsToOwner(target.owner_window, foreground_window);
  }
  return trusted_ui_process_id != 0 &&
         foreground_process == trusted_ui_process_id;
}

GenericWheelLongShotProfile::GenericWheelLongShotProfile()
    : GenericWheelLongShotProfile(
          {{static_cast<std::uint32_t>(GetCurrentProcessId())},
           static_cast<std::uint32_t>(GetCurrentProcessId()), true, 64}) {}

GenericWheelLongShotProfile::GenericWheelLongShotProfile(
    GenericWheelProfilePolicy policy)
    : policy_(policy) {}

const char* GenericWheelLongShotProfile::name() const noexcept {
  return "generic.wheel";
}

bool GenericWheelLongShotProfile::sameRequest(
    const LongShotRequest& request) const noexcept {
  return request_.owner_window == request.owner_window &&
         sameRect(request_.selectionRect(), request.selectionRect());
}

void GenericWheelLongShotProfile::clear() const noexcept {
  request_ = LongShotRequest{};
  target_ = GenericScrollTarget{};
  input_count_ = 0;
}

bool GenericWheelLongShotProfile::resolve(
    const LongShotRequest& request, LongShotProfileResult& out) const {
  out = LongShotProfileResult{};
  if (cancelled_.load()) return false;

  if (!target_.valid() || !sameRequest(request)) {
    clear();
    const auto resolution =
        resolveGenericScrollTarget(request, policy_.target_policy);
    status_ = resolution.status;
    if (!resolution.resolved()) return false;
    request_ = request;
    target_ = resolution.target;
  } else if (!validateGenericScrollTarget(request, target_,
                                           policy_.target_policy)) {
    status_ = GenericScrollTargetStatus::TargetUnavailable;
    target_ = GenericScrollTarget{};
    return false;
  }

  status_ = GenericScrollTargetStatus::Resolved;
  out = LongShotProfileResult{target_.target_window, target_.content};
  return out.valid();
}

bool GenericWheelLongShotProfile::foregroundIsSafe() const noexcept {
  if (!policy_.enforce_foreground) return true;
  return genericWheelForegroundIsSafe(
      target_, policy_.trusted_ui_process_id,
      reinterpret_cast<std::uintptr_t>(GetForegroundWindow()));
}

bool GenericWheelLongShotProfile::scrollDown(
    const LongShotRequest& request,
    const LongShotProfileResult& profile) const {
  if (cancelled_.load() || !sameRequest(request) || !target_.valid() ||
      profile.scroll_target != target_.target_window ||
      !sameRect(profile.content, target_.content) ||
      input_count_ >= policy_.max_inputs || !foregroundIsSafe() ||
      !validateGenericScrollTarget(request, target_,
                                   policy_.target_policy)) {
    return false;
  }
  ++input_count_;
  return sendBoundedWheelDown(target_.owner_window, target_.target_window,
                              target_.anchor_x, target_.anchor_y);
}

bool GenericWheelLongShotProfile::queryScrollState(
    const LongShotProfileResult&, LongShotScrollState& out) const {
  // Generic HWND targets often expose no meaningful SCROLLINFO. Image
  // continuity remains the authority for movement and terminal detection.
  out = LongShotScrollState{};
  return false;
}

void GenericWheelLongShotProfile::cancel() const noexcept {
  cancelled_.store(true);
}

GenericScrollTargetStatus
GenericWheelLongShotProfile::lastResolutionStatus() const noexcept {
  return status_;
}

int GenericWheelLongShotProfile::inputCount() const noexcept {
  return input_count_;
}

}  // namespace longshot_detail
}  // namespace qingying
