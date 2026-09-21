#include "generic_scroll_target_resolver.hpp"

#include "win32_scroll_helpers.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace qingying {
namespace longshot_detail {
namespace {

constexpr int kMaxChildDepth = 32;

class ScopedPhysicalCoordinates {
 public:
  ScopedPhysicalCoordinates() noexcept {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) return;
    set_context_ = reinterpret_cast<SetContextFn>(
        GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
    if (set_context_ != nullptr) {
      previous_ = set_context_(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
  }

  ~ScopedPhysicalCoordinates() {
    if (set_context_ != nullptr && previous_ != nullptr) {
      (void)set_context_(previous_);
    }
  }

  ScopedPhysicalCoordinates(const ScopedPhysicalCoordinates&) = delete;
  ScopedPhysicalCoordinates& operator=(const ScopedPhysicalCoordinates&) =
      delete;

 private:
  using SetContextFn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
  SetContextFn set_context_{nullptr};
  DPI_AWARENESS_CONTEXT previous_{nullptr};
};

bool safeRect(const ScreenPhysicalRect& rect) noexcept {
  if (!rect.valid()) return false;
  const auto right = static_cast<std::int64_t>(rect.x) + rect.width;
  const auto bottom = static_cast<std::int64_t>(rect.y) + rect.height;
  return right >= (std::numeric_limits<int>::min)() &&
         right <= (std::numeric_limits<int>::max)() &&
         bottom >= (std::numeric_limits<int>::min)() &&
         bottom <= (std::numeric_limits<int>::max)();
}

bool contains(const ScreenPhysicalRect& outer,
              const ScreenPhysicalRect& inner) noexcept {
  if (!safeRect(outer) || !safeRect(inner)) return false;
  const auto outer_right = static_cast<std::int64_t>(outer.x) + outer.width;
  const auto outer_bottom = static_cast<std::int64_t>(outer.y) + outer.height;
  const auto inner_right = static_cast<std::int64_t>(inner.x) + inner.width;
  const auto inner_bottom = static_cast<std::int64_t>(inner.y) + inner.height;
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner_right <= outer_right && inner_bottom <= outer_bottom;
}

bool belongsToOwner(HWND owner, HWND window) noexcept {
  return windowBelongsToOwner(reinterpret_cast<std::uintptr_t>(owner),
                              reinterpret_cast<std::uintptr_t>(window));
}

HWND deepestChildAt(HWND owner, POINT screen_point) noexcept {
  HWND current = owner;
  for (int depth = 0; depth < kMaxChildDepth; ++depth) {
    POINT client_point = screen_point;
    if (!ScreenToClient(current, &client_point)) return nullptr;
    HWND child = ChildWindowFromPointEx(
        current, client_point,
        CWP_SKIPDISABLED | CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
    if (child == nullptr || child == current) return current;
    if (!belongsToOwner(owner, child)) return nullptr;
    current = child;
  }
  return nullptr;
}

HWND directBranch(HWND owner, HWND window) noexcept {
  if (window == owner) return owner;
  HWND current = window;
  for (int depth = 0; depth < kMaxChildDepth && current != nullptr; ++depth) {
    const HWND parent = GetParent(current);
    if (parent == owner) return current;
    if (parent == nullptr || parent == current) return nullptr;
    current = parent;
  }
  return nullptr;
}

bool isAncestorOrSame(HWND ancestor, HWND window) noexcept {
  return ancestor == window || IsChild(ancestor, window);
}

HWND deepestCommonAncestor(HWND owner,
                           const std::vector<HWND>& windows) noexcept {
  if (windows.empty()) return nullptr;
  HWND candidate = windows.front();
  for (int depth = 0; depth < kMaxChildDepth && candidate != nullptr;
       ++depth) {
    bool contains_all = true;
    for (HWND window : windows) {
      if (!isAncestorOrSame(candidate, window)) {
        contains_all = false;
        break;
      }
    }
    if (contains_all) return candidate;
    if (candidate == owner) break;
    candidate = GetParent(candidate);
    if (!belongsToOwner(owner, candidate)) return nullptr;
  }
  return nullptr;
}

std::array<POINT, 5> samplePoints(const ScreenPhysicalRect& selection) {
  const auto right = static_cast<std::int64_t>(selection.x) +
                     selection.width - 1;
  const auto bottom = static_cast<std::int64_t>(selection.y) +
                      selection.height - 1;
  const auto center_x = static_cast<std::int64_t>(selection.x) +
                        selection.width / 2;
  const auto center_y = static_cast<std::int64_t>(selection.y) +
                        selection.height / 2;
  return {{{static_cast<LONG>(center_x), static_cast<LONG>(center_y)},
           {selection.x, selection.y},
           {static_cast<LONG>(right), selection.y},
           {selection.x, static_cast<LONG>(bottom)},
           {static_cast<LONG>(right), static_cast<LONG>(bottom)}}};
}

GenericScrollTargetResolution fail(GenericScrollTargetStatus status) {
  GenericScrollTargetResolution resolution;
  resolution.status = status;
  return resolution;
}

}  // namespace

bool GenericScrollTarget::valid() const noexcept {
  return owner_window != 0 && target_window != 0 && owner_process_id != 0 &&
         target_process_id != 0 && safeRect(content) &&
         anchor_x >= content.x && anchor_y >= content.y &&
         static_cast<std::int64_t>(anchor_x) <
             static_cast<std::int64_t>(content.x) + content.width &&
         static_cast<std::int64_t>(anchor_y) <
             static_cast<std::int64_t>(content.y) + content.height;
}

GenericScrollTargetResolution resolveGenericScrollTarget(
    const LongShotRequest& request) {
  return resolveGenericScrollTarget(
      request, {static_cast<std::uint32_t>(GetCurrentProcessId())});
}

GenericScrollTargetResolution resolveGenericScrollTarget(
    const LongShotRequest& request, GenericScrollTargetPolicy policy) {
  ScopedPhysicalCoordinates physical_coordinates;
  if (!request.valid() || !safeRect(request.selectionRect())) {
    return fail(GenericScrollTargetStatus::InvalidRequest);
  }
  const HWND owner = reinterpret_cast<HWND>(request.owner_window);
  if (owner == nullptr || !IsWindow(owner)) {
    return fail(GenericScrollTargetStatus::OwnerUnavailable);
  }
  const std::uint32_t owner_process_id = windowProcessId(request.owner_window);
  if (owner_process_id == 0) {
    return fail(GenericScrollTargetStatus::OwnerUnavailable);
  }
  if (policy.excluded_process_id != 0 &&
      owner_process_id == policy.excluded_process_id) {
    return fail(GenericScrollTargetStatus::SelfUi);
  }

  ScreenPhysicalRect owner_content;
  if (!windowClientScreenRect(request.owner_window, owner_content) ||
      !safeRect(owner_content)) {
    return fail(GenericScrollTargetStatus::OwnerUnavailable);
  }
  const ScreenPhysicalRect selection = request.selectionRect();
  if (!contains(owner_content, selection)) {
    return fail(GenericScrollTargetStatus::SelectionOutsideOwner);
  }

  const auto points = samplePoints(selection);
  std::vector<HWND> hits;
  hits.reserve(points.size());
  HWND branch = nullptr;
  for (POINT point : points) {
    const HWND hit = deepestChildAt(owner, point);
    if (hit == nullptr || !IsWindow(hit) || !belongsToOwner(owner, hit)) {
      return fail(GenericScrollTargetStatus::HitTestFailed);
    }
    const std::uint32_t hit_process_id = windowProcessId(
        reinterpret_cast<std::uintptr_t>(hit));
    if (hit_process_id == 0) {
      return fail(GenericScrollTargetStatus::TargetUnavailable);
    }
    if (policy.excluded_process_id != 0 &&
        hit_process_id == policy.excluded_process_id) {
      return fail(GenericScrollTargetStatus::SelfUi);
    }
    const HWND hit_branch = directBranch(owner, hit);
    if (hit_branch == nullptr) {
      return fail(GenericScrollTargetStatus::HitTestFailed);
    }
    if (branch == nullptr) {
      branch = hit_branch;
    } else if (branch != hit_branch) {
      return fail(GenericScrollTargetStatus::CrossPane);
    }
    bool duplicate = false;
    for (HWND existing : hits) {
      if (existing == hit) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) hits.push_back(hit);
  }

  const HWND target = deepestCommonAncestor(owner, hits);
  if (target == nullptr || !IsWindow(target) || !belongsToOwner(owner, target)) {
    return fail(GenericScrollTargetStatus::TargetUnavailable);
  }
  ScreenPhysicalRect target_content;
  if (!windowClientScreenRect(reinterpret_cast<std::uintptr_t>(target),
                              target_content) ||
      !safeRect(target_content) ||
      !contains(target_content, selection)) {
    return fail(GenericScrollTargetStatus::CrossPane);
  }
  const std::uint32_t target_process_id = windowProcessId(
      reinterpret_cast<std::uintptr_t>(target));
  if (target_process_id == 0) {
    return fail(GenericScrollTargetStatus::TargetUnavailable);
  }
  if (policy.excluded_process_id != 0 &&
      target_process_id == policy.excluded_process_id) {
    return fail(GenericScrollTargetStatus::SelfUi);
  }

  GenericScrollTargetResolution resolution;
  resolution.status = GenericScrollTargetStatus::Resolved;
  resolution.target.owner_window = request.owner_window;
  resolution.target.target_window = reinterpret_cast<std::uintptr_t>(target);
  resolution.target.owner_process_id = owner_process_id;
  resolution.target.target_process_id = target_process_id;
  resolution.target.anchor_x = points.front().x;
  resolution.target.anchor_y = points.front().y;
  resolution.target.content = target_content;
  return resolution;
}

bool validateGenericScrollTarget(
    const LongShotRequest& request, const GenericScrollTarget& target) {
  return validateGenericScrollTarget(
      request, target,
      {static_cast<std::uint32_t>(GetCurrentProcessId())});
}

bool validateGenericScrollTarget(
    const LongShotRequest& request, const GenericScrollTarget& target,
    GenericScrollTargetPolicy policy) {
  if (!target.valid() || request.owner_window != target.owner_window) {
    return false;
  }
  const auto current = resolveGenericScrollTarget(request, policy);
  return current.resolved() &&
         current.target.owner_window == target.owner_window &&
         current.target.target_window == target.target_window &&
         current.target.owner_process_id == target.owner_process_id &&
         current.target.target_process_id == target.target_process_id &&
         current.target.anchor_x == target.anchor_x &&
         current.target.anchor_y == target.anchor_y &&
         current.target.content == target.content;
}

}  // namespace longshot_detail
}  // namespace qingying
