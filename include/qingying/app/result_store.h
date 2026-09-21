#pragma once

#include "qingying/action/image.hpp"
#include "qingying/capture/capture_types.hpp"
#include "qingying/app/result_budget.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <functional>
#include <vector>

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

// Immutable ownership retained independently of the store and its scope slot.
class ResultLease {
 public:
  explicit operator bool() const noexcept { return image_ != nullptr; }
  const Image* image() const noexcept { return image_.get(); }
  const CapturedResult& metadata() const noexcept { return metadata_; }

 private:
  friend class ResultStore;
  std::shared_ptr<const Image> image_;
  CapturedResult metadata_;
};

// UI-thread-owned slots, at most one published result per trusted scope.
// Acquired leases may outlive the store and be consumed on worker threads.
// Capture entry points clear the previous result before attempting a new
// capture, so a failed capture leaves the store empty instead of retaining a
// large stale pixel buffer.
class ResultStore {
 public:
  // The injected monotonic clock must not throw; store operations run on UI.
  using Clock = std::function<std::chrono::steady_clock::time_point()>;
  explicit ResultStore(AutomationLimits limits = {}, Clock clock = {});
  ~ResultStore() { clearAll(); }
  ImageMemorySnapshot imageMemorySnapshot() const noexcept { return ImageMemoryBudget::global().snapshot(); }
  ResultStore(const ResultStore&) = delete;
  ResultStore& operator=(const ResultStore&) = delete;
  ResultBudget::Reservation reserve(ResultScopeId scope, int width, int height,
                                    bool ordinary_capture = true,
                                    std::uint64_t capacity_bytes = 0);
  ResultId publish(ResultScopeId scope, Image image,
                   ResultBudget::Reservation reservation,
                   ScreenPhysicalRect bounds = {});
  void sweep() noexcept;
  int resultStatus(ResultScopeId scope, ResultId id) const noexcept;
  int releaseResult(ResultScopeId scope, ResultId id) noexcept;
  std::optional<std::chrono::milliseconds> expiresIn(
      ResultScopeId scope, ResultId id) const noexcept;
  ResultBudgetSnapshot budgetSnapshot() const noexcept { return budget_.snapshot(); }
  // Shared accounting remains observable after this store is destroyed.
  ResultBudget budgetObserver() const { return budget_; }
  std::size_t tombstoneCount() const noexcept { return tombstones_.size(); }
  ResultId publish(ResultScopeId scope, Image image,
                   ScreenPhysicalRect bounds = {});
  ResultLease acquire(ResultScopeId scope, ResultId id) const noexcept;
  ResultLease acquire(ResultScopeId scope,
                      const ResultSelection& selection) const noexcept;
  ResultId currentId(ResultScopeId scope) const noexcept;
  void clearScope(ResultScopeId scope) noexcept;
  void clearAll() noexcept;  // Application-wide shutdown only.
  void release(ResultScopeId scope, ResultId id) noexcept;
  // Replaces the current image and returns a new id, or kInvalidResultId when
  // image is malformed.
  ResultId publish(Image image);

  // Returns a copy of the requested result so the stored image remains owned
  // by this store and can be selected explicitly by downstream actions.
  std::optional<ResultSnapshot> get(ResultId result_id) const;
  std::optional<ResultSnapshot> current() const;

  // GUI-only compatibility views, invalidated by slot replacement/clear.
  // Consumers that may reenter the message loop must use acquire instead.
  const Image* getImage(ResultId result_id) const noexcept;
  const Image* currentImage() const noexcept;
  ResultId currentId() const noexcept { return currentId(kGuiResultScopeId); }

  // Resolves current-result or explicit-result selection without exposing the
  // store's internal storage to action handlers.
  ResultId resolve(const ResultSelection& selection) const noexcept;

  // Releases the current image when the owning operation has finished.
  void release(ResultId result_id) noexcept;

  // GUI-only reset. Application teardown uses clearAll; ids are not reused.
  void clear() noexcept;

 private:
  enum class Invalidation { Expired, Released, Replaced };
  struct Tombstone {
    ResultScopeId scope;
    ResultId id;
    Invalidation reason;
    std::chrono::steady_clock::time_point until;
  };
  bool expired(const ResultLease& lease) const noexcept;
  void remember(ResultScopeId scope, const ResultLease& lease,
                Invalidation reason) noexcept;
  void pruneTombstones() noexcept;
  static bool isValidImage(const Image& image) noexcept;
  ResultId allocateId();

  AutomationLimits limits_;
  Clock clock_;
  ResultBudget budget_;
  std::vector<Tombstone> tombstones_;
  std::size_t max_tombstones_{0};
  std::unordered_map<ResultScopeId, ResultLease> slots_;
  ResultId next_id_{1};
};

}  // namespace qingying
