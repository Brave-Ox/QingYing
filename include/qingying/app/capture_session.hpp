#pragma once

#include "qingying/app/result_store.h"

#include <optional>
#include <utility>

namespace qingying {

// Compatibility facade for callers that still use the old "latest image"
// session API. New code should depend on ResultStore and select a ResultId or
// ResultSnapshot explicitly.
class CaptureSession {
 public:
  CaptureSession() = default;
  explicit CaptureSession(ResultStore& store) noexcept : store_(&store) {}

  CaptureSession(const CaptureSession&) = delete;
  CaptureSession& operator=(const CaptureSession&) = delete;

  bool hasResult() const { return store_->currentId() != kInvalidResultId; }

  const Image& result() const {
    const Image* image = store_->currentImage();
    if (image != nullptr) {
      return *image;
    }
    static const Image empty_image;
    return empty_image;
  }

  std::optional<ResultSnapshot> current() const {
    return store_->current();
  }

  ResultId currentId() const noexcept { return store_->currentId(); }

  ResultStore& store() noexcept { return *store_; }
  const ResultStore& store() const noexcept { return *store_; }

  void setResult(Image image) {
    (void)store_->publish(kGuiResultScopeId, std::move(image));
  }

  void clear() { store_->clearScope(kGuiResultScopeId); }

 private:
  ResultStore owned_store_;
  ResultStore* store_{&owned_store_};
};

}  // namespace qingying
