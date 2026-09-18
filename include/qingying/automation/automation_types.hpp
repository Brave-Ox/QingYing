#pragma once

#include "qingying/action/action_request.hpp"
#include "qingying/operation/operation_snapshot.hpp"
#include <functional>
#include <optional>
#include <string>
#include <variant>

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

struct ExecuteActionRequest {
  ActionPayload payload{StatusRequest{}};
  // Only Save/Copy/Pin accept this connection-local idempotency key. The key
  // does not authorize replay after reconnect or application restart.
  std::optional<std::string> request_key;
  // Public result consumers carry an opaque handle until the authenticated
  // endpoint resolves it to the connection-owned numeric ResultId.
  std::optional<ResultHandle> result_handle;
};

struct BeginLongShotRequest {};
struct GetOperationRequest {
  OperationId operation_id{kInvalidOperationId};
  std::optional<OperationHandle> operation_handle;
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
  std::optional<OperationHandle> operation_handle;
};

struct ReleaseResultRequest {
  ResultId result_id{kInvalidResultId};
  std::optional<ResultHandle> result_handle;
};

using AutomationPayload =
    std::variant<ExecuteActionRequest, BeginLongShotRequest, GetOperationRequest,
                 CancelOperationRequest, ReleaseResultRequest>;

// Private client/application DTO, NOT the public tool schema. Numeric IDs are
// for trusted internal callers. MCP control requests carry an opaque handle
// instead; the endpoint resolves it against the authenticated connection before
// execution. Supplying both forms is invalid. No caller-provided scope, operation ID for execute,
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
  // Local transport fact, never read from the wire. A disconnected client
  // retains its original connection identity but cannot reach the application.
  bool transport_available{true};
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
  std::optional<ResultHandle> result_handle;
  std::optional<OperationHandle> operation_handle;
};

using AutomationCompletion = std::function<void(AutomationResponse)>;

}  // namespace qingying
