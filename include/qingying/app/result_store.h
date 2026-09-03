#pragma once

#include "qingying/action/image.hpp"

#include <cstdint>
#include <map>
#include <optional>

namespace qingying {

using ResultId = std::uint64_t;

constexpr ResultId kInvalidResultId = 0;

// A stable, explicitly selected capture result. The image is copied out of
// ResultStore so callers cannot mutate the stored result through a snapshot.
struct ResultSnapshot {
  ResultId result_id{kInvalidResultId};
  Image image;

  bool empty() const noexcept {
    return result_id == kInvalidResultId || image.empty();
  }
};

// Owns published capture results and the current-result selection. A failed
// capture never reaches this store, so it cannot replace the current result.
class ResultStore {
 public:
  // Returns a new id, or kInvalidResultId when image is malformed.
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

  // Explicit reset for session/application teardown. Publishing a new result
  // does not discard older ids, which keeps explicit result selection valid;
  // ids are not reused after clear().
  void clear() noexcept;

 private:
  static bool isValidImage(const Image& image) noexcept;
  ResultId allocateId();

  std::map<ResultId, Image> results_;
  ResultId current_id_{kInvalidResultId};
  ResultId next_id_{1};
};

}  // namespace qingying
