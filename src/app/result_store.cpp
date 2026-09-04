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
  ResultId candidate = next_id_;
  if (candidate == kInvalidResultId) {
    candidate = 1;
  }
  next_id_ = candidate + 1;
  if (next_id_ == kInvalidResultId) {
    next_id_ = 1;
  }
  return candidate;
}

ResultId ResultStore::publish(Image image) {
  if (!isValidImage(image)) {
    return kInvalidResultId;
  }

  const ResultId result_id = allocateId();
  if (result_id == kInvalidResultId) {
    return kInvalidResultId;
  }

  // Release the old pixel buffer before taking ownership of the new one.
  // This also makes the replacement semantics explicit when a caller does
  // not clear at the beginning of its operation.
  current_image_.reset();
  current_image_.emplace(std::move(image));
  current_id_ = result_id;
  return result_id;
}

std::optional<ResultSnapshot> ResultStore::get(ResultId result_id) const {
  const Image* image = getImage(result_id);
  if (image == nullptr) {
    return std::nullopt;
  }
  return ResultSnapshot{result_id, *image};
}

std::optional<ResultSnapshot> ResultStore::current() const {
  if (current_id_ == kInvalidResultId) {
    return std::nullopt;
  }
  return get(current_id_);
}

const Image* ResultStore::getImage(ResultId result_id) const noexcept {
  if (result_id == kInvalidResultId) {
    return nullptr;
  }

  if (result_id != current_id_ || !current_image_) {
    return nullptr;
  }
  return &*current_image_;
}

const Image* ResultStore::currentImage() const noexcept {
  return getImage(current_id_);
}

ResultId ResultStore::resolve(const ResultSelection& selection) const noexcept {
  if (selection.kind == ResultSelectionKind::Current) {
    return selection.result_id == kInvalidResultId ? current_id_
                                                    : kInvalidResultId;
  }
  if (selection.kind == ResultSelectionKind::Explicit &&
      selection.result_id != kInvalidResultId) {
    return selection.result_id;
  }
  return kInvalidResultId;
}

void ResultStore::clear() noexcept {
  current_image_.reset();
  current_id_ = kInvalidResultId;
}

void ResultStore::release(ResultId result_id) noexcept {
  if (result_id == current_id_) {
    clear();
  }
}

}  // namespace qingying
