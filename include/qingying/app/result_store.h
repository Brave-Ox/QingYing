#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"

#include <cstdint>
#include <optional>

namespace qingying {

// A stable, explicitly selected capture result. The image is copied out of
// ResultStore so callers cannot mutate the stored result through a snapshot.
struct ResultSnapshot {
  ResultId result_id{kInvalidResultId};
  Image image;

  bool empty() const noexcept {
    return result_id == kInvalidResultId || image.empty();
  }
};

// Owns at most one published capture result and the current-result selection.
// Capture entry points clear the previous result before attempting a new
// capture, so a failed capture leaves the store empty instead of retaining a
// large stale pixel buffer.
class ResultStore {
 public:
  // Replaces the current image and returns a new id, or kInvalidResultId when
  // image is malformed.
  ResultId publish(Image image);

  // Returns a copy of the requested result so the stored image remains owned
  // by this store and can be selected explicitly by downstream actions.
  std::optional<ResultSnapshot> get(ResultId result_id) const;
  std::optional<ResultSnapshot> current() const;

  // Non-owning views used by compatibility adapters and action services that
  // only need to pass the image to another synchronous operation.
  const Image* getImage(ResultId result_id) const noexcept;
  const Image* currentImage() const noexcept;
  ResultId currentId() const noexcept { return current_id_; }

  // Resolves current-result or explicit-result selection without exposing the
  // store's internal storage to action handlers.
  ResultId resolve(const ResultSelection& selection) const noexcept;

  // Releases the current image when the owning operation has finished.
  void release(ResultId result_id) noexcept;

  // Explicit reset for session/application teardown. Ids are not reused after
  // clear().
  void clear() noexcept;

 private:
  static bool isValidImage(const Image& image) noexcept;
  ResultId allocateId();

  std::optional<Image> current_image_;
  ResultId current_id_{kInvalidResultId};
  ResultId next_id_{1};
};

}  // namespace qingying
