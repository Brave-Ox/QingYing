#pragma once

#include "qingying/action/common_types.hpp"
#include <atomic>
#include <memory>
#include <utility>

namespace qingying {

// A copyable cancellation handle for synchronous and future asynchronous
// action entry points. A default token is not cancelled; the source owns the
// only mutating operation.
class CancellationToken {
 public:
  CancellationToken() = default;

  bool isCancellationRequested() const noexcept {
    return state_ != nullptr && state_->load(std::memory_order_relaxed);
  }

 private:
  friend class CancellationSource;

  explicit CancellationToken(std::shared_ptr<std::atomic_bool> state)
      : state_(std::move(state)) {}

  std::shared_ptr<std::atomic_bool> state_;
};

class CancellationSource {
 public:
  CancellationSource()
      : state_(std::make_shared<std::atomic_bool>(false)) {}

  CancellationToken token() const noexcept { return CancellationToken{state_}; }

  void cancel() noexcept { state_->store(true, std::memory_order_relaxed); }

 private:
  std::shared_ptr<std::atomic_bool> state_;
};

enum class OperationState {
  Queued,
  AwaitingUser,
  Running,
  Paused,
  Finalizing,
  Cancelling,
  Succeeded,
  Failed,
  Cancelled,
  TimedOut,
};

constexpr bool isTerminal(OperationState state) noexcept {
  return state == OperationState::Succeeded || state == OperationState::Failed ||
         state == OperationState::Cancelled || state == OperationState::TimedOut;
}

enum class AbortReason { None, UserCancel, ClientCancel, Deadline, Disconnect,
                         Shutdown };
enum class ResultAvailability { None, Available, Released, Expired };

struct OperationControlStatus {
  AbortReason abort_reason{AbortReason::None};
  bool committed{false};
  bool settled{false};
};

// Small thread-safe execution boundary for workflow/export. Implementations
// arbitrate cancellation and the one commit permission at the same lock.
// Commit permission is not completion: actual execution must still report its
// outcome to the UI owner. I/O may request cancellation, never settle records.
class IOperationControl {
 public:
  virtual ~IOperationControl() = default;
  virtual bool requestCancel(AbortReason reason) = 0;
  virtual bool tryCommit() = 0;
  virtual OperationControlStatus status() = 0;
};

}  // namespace qingying
