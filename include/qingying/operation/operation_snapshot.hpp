#pragma once

#include "qingying/action/action_result.hpp"
#include "qingying/operation/operation_types.hpp"
#include <chrono>
#include <optional>
#include <string>

namespace qingying {

struct OperationProgress {
  std::string stage;
  std::uint64_t frames{0};
  int width{0};
  int height{0};
  // User stop may succeed with a partial image; it is not an abort reason.
  std::string stop_reason;
};

// Metadata only: never owns an Image or a result lease. Result expiry/release
// changes availability, not a succeeded operation's state or outcome.
struct OperationSnapshot {
  OperationId operation_id{kInvalidOperationId};
  OperationState state{OperationState::Queued};
  OperationProgress progress;
  AbortReason abort_reason{AbortReason::None};
  bool committed{false};
  ResultAvailability result_availability{ResultAvailability::None};
  std::chrono::steady_clock::time_point submitted_at{};
  std::chrono::steady_clock::time_point updated_at{};
  std::optional<std::chrono::steady_clock::time_point> completed_at;
  // Present only after a terminal state; diagnostics and typed metadata survive
  // result expiry. A query of a failed operation is itself a successful query.
  std::optional<ActionResult> outcome;
};

}  // namespace qingying
