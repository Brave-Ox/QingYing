#include "qingying/app/result_store.h"

#include <limits>
#include <utility>

namespace qingying {

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
  if (scope == kInvalidResultScopeId || !isValidImage(image)) {
    return kInvalidResultId;
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
  lease.image_ = std::make_shared<const Image>(std::move(image));
  slots_[scope] = std::move(lease);
  return id;
}

ResultId ResultStore::publish(Image image) {
  return publish(kGuiResultScopeId, std::move(image));
}

ResultLease ResultStore::acquire(ResultScopeId scope, ResultId id) const noexcept {
  const auto it = slots_.find(scope);
  if (it == slots_.end() || id == kInvalidResultId ||
      it->second.metadata().result_id != id) return {};
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
  return it == slots_.end() ? kInvalidResultId : it->second.metadata().result_id;
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

void ResultStore::clearScope(ResultScopeId scope) noexcept { slots_.erase(scope); }
void ResultStore::clearAll() noexcept { slots_.clear(); }
void ResultStore::clear() noexcept { clearScope(kGuiResultScopeId); }

void ResultStore::release(ResultScopeId scope, ResultId id) noexcept {
  if (id != kInvalidResultId && currentId(scope) == id) clearScope(scope);
}
void ResultStore::release(ResultId id) noexcept { release(kGuiResultScopeId, id); }

}  // namespace qingying
