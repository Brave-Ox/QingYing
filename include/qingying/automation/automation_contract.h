#pragma once

#include "qingying/action/types.hpp"

#include <functional>
#include <type_traits>

namespace qingying {

// Internal identities are never exposed as public numeric handles. An epoch
// identifies an application instance; generations are unique within that epoch
// across ALL connections, including reconnects and interface disable/enable.
using ApplicationEpoch = std::uint64_t;
using ConnectionGeneration = std::uint64_t;
constexpr ApplicationEpoch kInvalidApplicationEpoch = 0;
constexpr ConnectionGeneration kInvalidConnectionGeneration = 0;

struct AutomationConnection {
  ApplicationEpoch application_epoch{kInvalidApplicationEpoch};
  ConnectionGeneration generation{kInvalidConnectionGeneration};

  constexpr bool valid() const noexcept {
    return application_epoch != kInvalidApplicationEpoch &&
           generation != kInvalidConnectionGeneration;
  }
};

constexpr bool sameConnection(const AutomationConnection& a,
                              const AutomationConnection& b) noexcept {
  return a.valid() && b.valid() &&
         a.application_epoch == b.application_epoch &&
         a.generation == b.generation;
}

// The trusted endpoint creates this context after authentication. It is NOT a
// wire DTO: scope, cancellation state and monotonic admission time must never
// be decoded from request fields. Requeueing must preserve submitted_at.
struct TrustedAutomationContext {
  AutomationConnection connection;
  ActionContext action;
  CancellationToken cancellation;
  std::chrono::steady_clock::time_point submitted_at{
      std::chrono::steady_clock::now()};

  bool valid() const noexcept {
    return connection.valid() &&
           action.result_scope != kInvalidResultScopeId &&
           action.result_scope != kGuiResultScopeId;
  }
};

// Distinct public handle types prevent accidental result/operation interchange.
// Only the trusted handle map may resolve these to internal numeric IDs. That
// map must bind every entry to the application epoch AND connection generation;
// a syntactically valid handle alone confers no authorization. Do not stringify
// a uint64 ID to implement the public handle mapping.
template <typename Tag>
struct OpaqueAutomationHandle {
  std::string value;

  bool valid() const noexcept {
    return !value.empty() && value.find('\0') == std::string::npos;
  }
};

struct ResultHandleTag {};
struct OperationHandleTag {};
using ResultHandle = OpaqueAutomationHandle<ResultHandleTag>;
using OperationHandle = OpaqueAutomationHandle<OperationHandleTag>;

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

struct ExecuteActionRequest {
  ActionPayload payload{StatusRequest{}};
  // Only Save/Copy/Pin accept this connection-local idempotency key. The key
  // does not authorize replay after reconnect or application restart.
  std::optional<std::string> request_key;
};

struct BeginLongShotRequest {};
struct GetOperationRequest {
  OperationId operation_id{kInvalidOperationId};
};

// Wrappers are required because RequestId and OperationId share uint64 storage.
// This variant makes simultaneous request and operation targets impossible.
struct OperationCancellation {
  OperationId operation_id{kInvalidOperationId};
};
struct RequestCancellation {
  RequestId request_id{kInvalidRequestId};
};
using CancellationTarget =
    std::variant<OperationCancellation, RequestCancellation>;

struct CancelOperationRequest {
  // RequestCancellation is PRIVATE transport control, used before the caller
  // knows the operation ID. The public cancel_operation tool accepts only an
  // OperationHandle, resolved to OperationCancellation by the trusted map.
  CancellationTarget target{OperationCancellation{}};
};

struct ReleaseResultRequest {
  ResultId result_id{kInvalidResultId};
};

using AutomationPayload =
    std::variant<ExecuteActionRequest, BeginLongShotRequest, GetOperationRequest,
                 CancelOperationRequest, ReleaseResultRequest>;

// Private client/application DTO, NOT the public tool schema. Numeric IDs have
// already passed through the connection-bound opaque handle map. The endpoint
// still verifies ownership. No caller-provided scope, operation ID for execute,
// cancellation token or clock timestamp is accepted here.
struct AutomationRequest {
  RequestId request_id{kInvalidRequestId};
  // Unset selects configured defaults (180s for begin_longshot). An explicit
  // duration must be positive. The trusted admission time starts the deadline,
  // including queueing; receiving/dequeueing must not restart that deadline.
  std::optional<std::chrono::milliseconds> timeout;
  AutomationPayload payload{ExecuteActionRequest{}};
};

struct CancellationResult {
  CancellationTarget target{OperationCancellation{}};
  // May be invalid when cancelling a request not yet assigned an operation.
  OperationId operation_id{kInvalidOperationId};
  OperationState state{OperationState::Queued};
  // True means recorded/requested, not that a worker has stopped. A repeated
  // cancel can return the existing terminal state with this flag false.
  bool cancellation_requested{false};
};

struct ReleasedResult {
  ResultId result_id{kInvalidResultId};
  bool already_released{false};
};

using AutomationControlOutput =
    std::variant<std::monostate, OperationSnapshot, CancellationResult,
                 ReleasedResult>;

struct AutomationResponse {
  AutomationConnection connection;
  // request_id always identifies this invocation. operation_id is the executed
  // or queried/cancelled operation, or zero for release, rejection before an
  // operation exists, and RequestId cancellation before operation allocation.
  // execute uses result.output; failures may also carry WindowCandidates.
  ActionResult result;
  // execute -> monostate; begin/get -> OperationSnapshot; cancel ->
  // CancellationResult; release -> ReleasedResult. Rejection -> monostate.
  // A failed operation's outcome belongs inside the snapshot: result.ok stays
  // true when get_operation successfully found that operation.
  AutomationControlOutput control;
};

using AutomationCompletion = std::function<void(AutomationResponse)>;

class IAutomationClient {
 public:
  virtual ~IAutomationClient() = default;

  // Each client instance is permanently bound to one authenticated epoch and
  // generation. Reconnect creates a NEW instance; a disconnected/closed client
  // rejects submissions. Never migrate or replay old requests on a new
  // connection, including after resolving a public handle to a numeric ID.
  // Exactly one completion per submit with a non-empty callback, including
  // validation/admission failure, exceptions and connection loss. Synchronous
  // completion is allowed; callers must establish tracking before submit.
  // execute completes after actual execution; begin_longshot completes once
  // the UI has admitted and started selection (or failed to start it).
  // Implementations document their completion executor; consumers must not
  // assume a UI thread or throw from the callback. Echo the originating
  // connection identity even if completion races reconnect/close.
  virtual void submit(AutomationRequest request,
                      AutomationCompletion completion) = 0;

  // Idempotently stop admission, cancel/drain pending work and settle all
  // callbacks before returning. Never exits the desktop application. Concrete
  // transport implementations own their I/O cancellation and worker cleanup.
  virtual void close() noexcept = 0;
};

inline std::chrono::milliseconds automationTimeout(
    const AutomationRequest& request, const AutomationLimits& limits) noexcept {
  return request.timeout.value_or(
      std::holds_alternative<BeginLongShotRequest>(request.payload)
          ? limits.default_longshot_timeout : limits.default_request_timeout);
}

// Shape/ID/deadline validation. String/frame/pixel limits and platform path policy
// are enforced by the codec and endpoint using the injected AutomationLimits.
// These checks confer neither ownership nor permission to execute an action.
inline ActionValidationResult validateAutomationRequest(
    const AutomationRequest& request,
    const AutomationLimits& limits = AutomationLimits{}) {
  if (!limits.valid()) {
    return {false, "automation limits are invalid"};
  }
  if (request.request_id == kInvalidRequestId) {
    return {false, "request_id must be nonzero"};
  }
  if (request.timeout && (request.timeout->count() <= 0 ||
                          *request.timeout > limits.max_request_timeout)) {
    return {false, "explicit timeout is outside configured bounds"};
  }
  return std::visit(
      [&request](const auto& payload) -> ActionValidationResult {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, ExecuteActionRequest>) {
          const ActionType type = actionType(payload.payload);
          const bool consumes_result = type == ActionType::Save ||
                                       type == ActionType::Copy ||
                                       type == ActionType::Pin;
          if (payload.request_key &&
              (!consumes_result || payload.request_key->empty() ||
               payload.request_key->find('\0') != std::string::npos)) {
            return {false, "request_key requires Save, Copy or Pin and a value"};
          }
          const bool explicit_selection = std::visit(
              [](const auto& action) {
                using Action = std::decay_t<decltype(action)>;
                if constexpr (std::is_same_v<Action, SaveRequest> ||
                              std::is_same_v<Action, CopyRequest> ||
                              std::is_same_v<Action, PinRequest>) {
                  return action.result.kind == ResultSelectionKind::Explicit;
                } else {
                  return true;
                }
              }, payload.payload);
          if (!explicit_selection) {
            return {false, "automation consumption requires an explicit result"};
          }
          // Validate the unchanged ActionPayload before the endpoint allocates
          // its paired internal request/operation IDs and attaches context.
          return validateActionRequest(ActionRequest{payload.payload});
        } else if constexpr (std::is_same_v<Payload, BeginLongShotRequest>) {
          return {true, {}};
        } else if constexpr (std::is_same_v<Payload, GetOperationRequest>) {
          return {payload.operation_id != kInvalidOperationId,
                  payload.operation_id != kInvalidOperationId
                      ? "" : "operation_id must be nonzero"};
        } else if constexpr (std::is_same_v<Payload, CancelOperationRequest>) {
          const bool valid = std::visit([&request](const auto& target) {
            using Target = std::decay_t<decltype(target)>;
            if constexpr (std::is_same_v<Target, OperationCancellation>) {
              return target.operation_id != kInvalidOperationId;
            } else {
              return target.request_id != kInvalidRequestId &&
                     target.request_id != request.request_id;
            }
          }, payload.target);
          return {valid, valid ? "" : "invalid cancellation target"};
        } else {
          return {payload.result_id != kInvalidResultId,
                  payload.result_id != kInvalidResultId
                      ? "" : "result_id must be nonzero"};
        }
      }, request.payload);
}

}  // namespace qingying
