#include "qingying/app/result_store.h"

#include <limits>
#include <utility>
#include <algorithm>
#include <stdexcept>

namespace qingying {
namespace {
using SteadyClock = std::chrono::steady_clock;

SteadyClock::time_point deadline(SteadyClock::time_point now,
                                std::chrono::milliseconds ttl) noexcept {
  // Compare before duration_cast: injected policy may exceed clock range.
  using WideDuration = std::chrono::duration<long double>;
  const auto max_duration = (SteadyClock::duration::max)();
  if (WideDuration(ttl) >= WideDuration(max_duration)) return (SteadyClock::time_point::max)();
  const auto delta = std::chrono::duration_cast<SteadyClock::duration>(ttl);
  if (now.time_since_epoch() > max_duration - delta) return (SteadyClock::time_point::max)();
  return now + delta;
}

struct RetainedImage {
  // Reverse member destruction frees pixels BEFORE returning their budget.
  ResultBudget::Reservation reservation;
  Image image;
  RetainedImage(ResultBudget::Reservation held, Image pixels)
      : reservation(std::move(held)), image(std::move(pixels)) {}
};
}  // namespace

ResultStore::ResultStore(AutomationLimits limits, Clock clock)
    : limits_(limits), clock_(clock ? std::move(clock) : Clock{SteadyClock::now}),
      budget_(limits) {
  const auto count = static_cast<std::uint64_t>(limits.max_connections) *
                     limits.max_tombstones_per_connection;
  if (count > tombstones_.max_size()) throw std::length_error("result tombstone limit");
  max_tombstones_ = static_cast<std::size_t>(count);
  // No allocations in noexcept clear/release/sweep paths.
  tombstones_.reserve(max_tombstones_);
}

ResultBudget::Reservation ResultStore::reserve(
    ResultScopeId scope, int width, int height, bool ordinary_capture) {
  return budget_.reserve(scope, width, height, ordinary_capture);
}

bool ResultStore::isValidImage(const Image& image) noexcept {
  if (image.width <= 0 || image.height <= 0) {
    return false;
  }

  const std::size_t width = static_cast<std::size_t>(image.width);
  const std::size_t height = static_cast<std::size_t>(image.height);
  if (height != 0 && width > (std::numeric_limits<std::size_t>::max)() / height) {
    return false;
  }
  return image.pixels.size() == width * height;
}

ResultId ResultStore::allocateId() {
  // Exhaustion fails closed: never reuse an id still held by a consumer.
  if (next_id_ == kInvalidResultId) return kInvalidResultId;
  return next_id_++;
}

ResultId ResultStore::publish(ResultScopeId scope, Image image,
                              ScreenPhysicalRect bounds) {
  if (!isValidImage(image) || image.pixels.capacity() >
      (std::numeric_limits<std::uint64_t>::max)() / sizeof(std::uint32_t)) {
    return kInvalidResultId;
  }
  auto reservation = budget_.reserve(scope, image.width, image.height, true,
      static_cast<std::uint64_t>(image.pixels.capacity()) * sizeof(std::uint32_t));
  return publish(scope, std::move(image), std::move(reservation), bounds);
}

ResultId ResultStore::publish(ResultScopeId scope, Image image,
                              ResultBudget::Reservation reservation,
                              ScreenPhysicalRect bounds) {
  if (!reservation || !budget_.owns(reservation) || reservation.scope_ != scope ||
      reservation.width_ != image.width || reservation.height_ != image.height ||
      !isValidImage(image) || image.pixels.capacity() >
          reservation.bytes() / sizeof(std::uint32_t)) {
    return kInvalidResultId;
  }
  if (scope != kGuiResultScopeId && slots_.find(scope) == slots_.end()) {
    const auto external_slots = slots_.size() - slots_.count(kGuiResultScopeId);
    if (external_slots >= limits_.max_connections) return kInvalidResultId;
  }
  const ResultId id = allocateId();
  if (id == kInvalidResultId) return id;
  ResultLease lease;
  lease.metadata_.result_id = id;
  lease.metadata_.width = image.width;
  lease.metadata_.height = image.height;
  bounds.width = image.width;
  bounds.height = image.height;
  lease.metadata_.bounds = bounds;
  if (scope != kGuiResultScopeId) {
    lease.metadata_.expires_at = deadline(clock_(), limits_.result_ttl);
  }
  reservation.commit();
  auto storage = std::make_shared<RetainedImage>(std::move(reservation), std::move(image));
  lease.image_ = std::shared_ptr<const Image>(storage, &storage->image);
  const auto previous = slots_.find(scope);
  if (previous != slots_.end()) remember(scope, previous->second,
      expired(previous->second) ? Invalidation::Expired : Invalidation::Replaced);
  slots_[scope] = std::move(lease);
  return id;
}

ResultId ResultStore::publish(Image image) {
  return publish(kGuiResultScopeId, std::move(image));
}

ResultLease ResultStore::acquire(ResultScopeId scope, ResultId id) const noexcept {
  const auto it = slots_.find(scope);
  if (it == slots_.end() || id == kInvalidResultId ||
      it->second.metadata().result_id != id || expired(it->second)) return {};
  return it->second;
}

ResultLease ResultStore::acquire(
    ResultScopeId scope, const ResultSelection& selection) const noexcept {
  if (!selection.valid()) return {};
  return acquire(scope, selection.kind == ResultSelectionKind::Current
                            ? currentId(scope) : selection.result_id);
}

ResultId ResultStore::currentId(ResultScopeId scope) const noexcept {
  const auto it = slots_.find(scope);
  return it == slots_.end() || expired(it->second)
      ? kInvalidResultId : it->second.metadata().result_id;
}

std::optional<ResultSnapshot> ResultStore::get(ResultId id) const {
  const auto lease = acquire(kGuiResultScopeId, id);
  if (!lease) return std::nullopt;
  return ResultSnapshot{id, *lease.image()};
}

std::optional<ResultSnapshot> ResultStore::current() const {
  return get(currentId());
}

const Image* ResultStore::getImage(ResultId id) const noexcept {
  return acquire(kGuiResultScopeId, id).image();
}

const Image* ResultStore::currentImage() const noexcept {
  return getImage(currentId());
}

ResultId ResultStore::resolve(const ResultSelection& selection) const noexcept {
  // Legacy GUI selection helper; acquire performs the ownership check.
  if (!selection.valid()) return kInvalidResultId;
  return selection.kind == ResultSelectionKind::Current
             ? currentId() : selection.result_id;
}

void ResultStore::clearScope(ResultScopeId scope) noexcept {
  slots_.erase(scope);
  // Disconnect clears this scope's query history as well as its slot.
  tombstones_.erase(std::remove_if(tombstones_.begin(), tombstones_.end(),
      [scope](const Tombstone& t) { return t.scope == scope; }), tombstones_.end());
}
void ResultStore::clearAll() noexcept { slots_.clear(); tombstones_.clear(); }
void ResultStore::clear() noexcept { clearScope(kGuiResultScopeId); }

void ResultStore::release(ResultScopeId scope, ResultId id) noexcept {
  (void)releaseResult(scope, id);
}
void ResultStore::release(ResultId id) noexcept { release(kGuiResultScopeId, id); }

bool ResultStore::expired(const ResultLease& lease) const noexcept {
  const auto& expires = lease.metadata().expires_at;
  return expires && clock_() >= *expires;
}

void ResultStore::pruneTombstones() noexcept {
  const auto now = clock_();
  tombstones_.erase(std::remove_if(tombstones_.begin(), tombstones_.end(),
      [now](const Tombstone& t) { return now >= t.until; }), tombstones_.end());
}

void ResultStore::remember(ResultScopeId scope, const ResultLease& lease,
                           Invalidation reason) noexcept {
  if (scope == kGuiResultScopeId) return;
  pruneTombstones();
  const auto base = reason == Invalidation::Expired
      ? *lease.metadata().expires_at : clock_();
  const auto until = deadline(base, limits_.tombstone_ttl);
  if (clock_() >= until) return;
  const auto count = std::count_if(tombstones_.begin(), tombstones_.end(),
      [scope](const Tombstone& t) { return t.scope == scope; });
  if (static_cast<std::uint64_t>(count) >= limits_.max_tombstones_per_connection) {
    tombstones_.erase(std::find_if(tombstones_.begin(), tombstones_.end(),
        [scope](const Tombstone& t) { return t.scope == scope; }));
  }
  if (tombstones_.size() == max_tombstones_) tombstones_.erase(tombstones_.begin());
  tombstones_.push_back({scope, lease.metadata().result_id, reason, until});
}

void ResultStore::sweep() noexcept {
  pruneTombstones();
  for (auto it = slots_.begin(); it != slots_.end();) {
    if (expired(it->second)) {
      remember(it->first, it->second, Invalidation::Expired);
      it = slots_.erase(it);
    } else ++it;
  }
}

int ResultStore::resultStatus(ResultScopeId scope, ResultId id) const noexcept {
  const auto it = slots_.find(scope);
  if (it != slots_.end() && it->second.metadata().result_id == id) {
    return expired(it->second) ? ErrorCode::kResultExpired : ErrorCode::kOk;
  }
  for (const auto& t : tombstones_) {
    if (t.scope == scope && t.id == id && clock_() < t.until) {
      return t.reason == Invalidation::Expired
          ? ErrorCode::kResultExpired : ErrorCode::kResultNotFound;
    }
  }
  return ErrorCode::kResultNotFound;
}

int ResultStore::releaseResult(ResultScopeId scope, ResultId id) noexcept {
  const auto it = slots_.find(scope);
  if (it != slots_.end() && it->second.metadata().result_id == id) {
    const bool was_expired = expired(it->second);
    remember(scope, it->second, was_expired ? Invalidation::Expired : Invalidation::Released);
    slots_.erase(it);
    return was_expired ? ErrorCode::kResultExpired : ErrorCode::kOk;
  }
  for (const auto& t : tombstones_) {
    if (t.scope == scope && t.id == id && t.reason == Invalidation::Released && clock_() < t.until) {
      return ErrorCode::kOk;
    }
  }
  return resultStatus(scope, id);
}

std::optional<std::chrono::milliseconds> ResultStore::expiresIn(
    ResultScopeId scope, ResultId id) const noexcept {
  const auto lease = acquire(scope, id);
  if (!lease || !lease.metadata().expires_at) return std::nullopt;
  const auto now = clock_();
  if (now >= *lease.metadata().expires_at) return std::chrono::milliseconds{0};
  // A wide subtraction also handles clocks injected near the epoch limits.
  using Milliseconds = std::chrono::duration<long double, std::milli>;
  const auto remaining = Milliseconds(lease.metadata().expires_at->time_since_epoch()).count() -
                         Milliseconds(now.time_since_epoch()).count();
  const auto max = (std::chrono::milliseconds::max)().count();
  return std::chrono::milliseconds{remaining >= max ? max :
      static_cast<std::chrono::milliseconds::rep>(remaining)};
}

}  // namespace qingying
