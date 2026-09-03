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
  if (results_.size() >=
      static_cast<std::size_t>((std::numeric_limits<ResultId>::max)() - 1)) {
    return kInvalidResultId;
  }

  ResultId candidate = next_id_;
  do {
    if (candidate == kInvalidResultId) {
      candidate = 1;
    }
    if (results_.find(candidate) == results_.end()) {
      next_id_ = candidate + 1;
      if (next_id_ == kInvalidResultId) {
        next_id_ = 1;
      }
      return candidate;
    }
    ++candidate;
    if (candidate == kInvalidResultId) {
      candidate = 1;
    }
  } while (candidate != next_id_);

  return kInvalidResultId;
}

ResultId ResultStore::publish(Image image) {
  if (!isValidImage(image)) {
    return kInvalidResultId;
  }

  const ResultId result_id = allocateId();
  if (result_id == kInvalidResultId) {
    return kInvalidResultId;
  }

  results_.emplace(result_id, std::move(image));
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

  const auto it = results_.find(result_id);
  return it == results_.end() ? nullptr : &it->second;
}

const Image* ResultStore::currentImage() const noexcept {
  return getImage(current_id_);
}

void ResultStore::clear() noexcept {
  results_.clear();
  current_id_ = kInvalidResultId;
}

}  // namespace qingying
