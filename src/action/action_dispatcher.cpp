#include "qingying/action/action_dispatcher.hpp"

#include "qingying/diagnostics/fault_boundary.h"
#include <atomic>
#include <memory>
#include <type_traits>
#include <utility>

namespace qingying {

namespace {

ActionValidationResult invalidRequest(const char* message) {
  ActionValidationResult result;
  result.valid = false;
  result.message = message;
  return result;
}

ActionResult requestFailure(const ActionRequest& request, int error_code,
                            const std::string& message) {
  ActionResult result;
  result.request_id = request.request_id;
  result.operation_id = request.operation_id;
  result.ok = false;
  result.error_code = error_code;
  result.message = message;
  return result;
}

void correlate(const ActionRequest& request, ActionResult& result) noexcept {
  result.request_id = request.request_id;
  result.operation_id = request.operation_id;
}

ActionResult handlerFailure(const ActionRequest& request, const FaultDiagnostic& fault) {
  auto result = requestFailure(request, fault.error_code,
      fault.error_code == ErrorCode::kResourceLimit ? "resource limit" : "action handler threw an exception");
  result.diagnostic = fault;
  result.failure_stage = "handler";
  return result;
}

FaultContext requestDiagnosticContext(const ActionRequest& request) {
  auto context = request.diagnostic_context;
  context.request_id = request.request_id;
  context.operation_id = request.operation_id;
  context.scope_id = request.context.result_scope;
  context.started_at = request.submitted_at;
  return context;
}

struct CompletionState {
  std::atomic_bool delivered{false};
  ActionCompletion callback;
};

ActionCompletion atMostOnce(ActionCompletion completion) {
  auto state = std::make_shared<CompletionState>();
  state->callback = std::move(completion);
  const auto context = currentFaultContext();
  return [state, context](ActionResult result) mutable {
    DiagnosticScope scope(context);
    if (state->delivered.exchange(true, std::memory_order_acq_rel)) {
      return;
    }
    if (!state->callback) {
      return;
    }
    // A consumer callback is outside the action boundary. Do not let a
    // throwing callback cause a second completion or escape a worker thread.
    try {
      state->callback(std::move(result));
    } catch (...) {
      recordFault(ErrorCode::kUnknown, FaultOrigin::Handler, FaultDomain::Request);
    }
  };
}

}  // namespace

ActionDispatcher::ActionDispatcher(ActionExecutor executor)
    : executor_(std::move(executor)) {}

ActionValidationResult validateActionRequest(const ActionRequest& request) {
  if (!request.context.valid()) {
    return invalidRequest("result scope must be nonzero");
  }
  if ((request.request_id == kInvalidRequestId) !=
      (request.operation_id == kInvalidOperationId)) {
    return invalidRequest(
        "request_id and operation_id must be provided together");
  }
  if (request.timeout.count() < 0) {
    return invalidRequest("timeout must be zero or positive");
  }

  return std::visit(
      [](const auto& payload) -> ActionValidationResult {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, StatusRequest>) {
          return ActionValidationResult{true, {}};
        } else if constexpr (std::is_same_v<Payload,
                                            CaptureRegionRequest>) {
          return payload.region.valid()
                     ? ActionValidationResult{true, {}}
                     : invalidRequest("capture region must be non-empty");
        } else if constexpr (std::is_same_v<Payload,
                                            CaptureWindowRequest>) {
          if (payload.match != WindowMatchMode::Contains &&
              payload.match != WindowMatchMode::Exact) {
            return invalidRequest("window match mode is invalid");
          }
          return payload.window_query.empty() ||
                         (payload.process_id && *payload.process_id == 0)
                     ? invalidRequest("window query is required")
                     : ActionValidationResult{true, {}};
        } else if constexpr (std::is_same_v<Payload, CropCenterRequest>) {
          return payload.width > 0 && payload.height > 0
                     ? ActionValidationResult{true, {}}
                     : invalidRequest("crop dimensions must be positive");
        } else if constexpr (std::is_same_v<Payload, CopyRequest> ||
                             std::is_same_v<Payload, PinRequest>) {
          return payload.result.valid()
                     ? ActionValidationResult{true, {}}
                     : invalidRequest("result selection is invalid");
        } else if constexpr (std::is_same_v<Payload, SaveRequest>) {
          if (!payload.result.valid()) {
            return invalidRequest("result selection is invalid");
          }
          return payload.path.empty()
                     ? invalidRequest("save path is required")
                     : ActionValidationResult{true, {}};
        } else {
          return invalidRequest("unsupported action payload");
        }
      },
      request.payload);
}

void ActionDispatcher::registerHandler(std::unique_ptr<IActionHandler> handler) {
  if (!handler) {
    return;
  }
  const Key key = static_cast<Key>(handler->type());
  handlers_[key] = std::move(handler);
}

void ActionDispatcher::registerAsyncHandler(
    std::unique_ptr<IAsyncActionHandler> handler) {
  if (!handler) {
    return;
  }
  const Key key = static_cast<Key>(handler->type());
  async_handlers_[key] = std::move(handler);
}

bool ActionDispatcher::hasHandler(ActionType type) const noexcept {
  const auto key = static_cast<Key>(type);
  return handlers_.count(key) != 0 || async_handlers_.count(key) != 0;
}

void ActionDispatcher::setExecutor(ActionExecutor executor) {
  executor_ = std::move(executor);
}

ActionResult ActionDispatcher::dispatch(const ActionRequest& request) const {
  DiagnosticScope diagnostic_scope(requestDiagnosticContext(request));
  const ActionValidationResult validation = validateActionRequest(request);
  if (!validation.valid) {
    return requestFailure(request, ErrorCode::kInvalidArgument,
                          validation.message);
  }
  if (request.cancellation.isCancellationRequested()) {
    return requestFailure(request, ErrorCode::kCancelled,
                          "action cancelled before dispatch");
  }
  if (request.timedOut()) {
    return requestFailure(request, ErrorCode::kTimeout,
                          "action timed out before dispatch");
  }

  const Key key = static_cast<Key>(request.type());
  const auto it = handlers_.find(key);
  if (it == handlers_.end() || !it->second) {
    return requestFailure(request, ErrorCode::kNotImplemented,
                          "no handler registered for action");
  }

  ActionResult result;
  FaultDiagnostic fault;
  if (!containFault(FaultOrigin::Handler, FaultDomain::Request, [&] {
    result = it->second->handle(request);
  }, &fault)) {
    return handlerFailure(request, fault);
  }
  // Cancellation and deadlines are admission guards. Once a synchronous
  // handler has run, its result represents the side effects it committed.
  correlate(request, result);
  return result;
}

void ActionDispatcher::submit(const ActionRequest& request,
                              ActionCompletion completion) const {
  DiagnosticScope diagnostic_scope(requestDiagnosticContext(request));
  ActionCompletion complete = atMostOnce(
      [request, completion = std::move(completion)](ActionResult result) mutable {
        correlate(request, result);
        if (completion) {
          completion(std::move(result));
        }
      });

  const ActionValidationResult validation = validateActionRequest(request);
  if (!validation.valid) {
    complete(requestFailure(request, ErrorCode::kInvalidArgument,
                            validation.message));
    return;
  }
  if (request.cancellation.isCancellationRequested()) {
    complete(requestFailure(request, ErrorCode::kCancelled,
                            "action cancelled before dispatch"));
    return;
  }
  if (request.timedOut()) {
    complete(requestFailure(request, ErrorCode::kTimeout,
                            "action timed out before dispatch"));
    return;
  }

  const Key key = static_cast<Key>(request.type());
  const auto async_it = async_handlers_.find(key);
  if (async_it != async_handlers_.end() && async_it->second) {
    FaultDiagnostic fault;
    if (!containFault(FaultOrigin::Handler, FaultDomain::Request, [&] {
      // Copy the guarded callback so a handler may retain and invoke it later;
      // all copies share the same once state.
      async_it->second->handleAsync(request, complete, executor_);
    }, &fault)) {
      // If the handler completed before throwing, atMostOnce suppresses this
      // fallback and preserves the already delivered result.
      complete(handlerFailure(request, fault));
    }
    return;
  }

  const auto it = handlers_.find(key);
  if (it == handlers_.end() || !it->second) {
    complete(requestFailure(request, ErrorCode::kNotImplemented,
                            "no handler registered for action"));
    return;
  }

  FaultDiagnostic fault;
  if (!containFault(FaultOrigin::Handler, FaultDomain::Request, [&] {
    ActionResult result = it->second->handle(request);
    correlate(request, result);
    complete(std::move(result));
  }, &fault)) {
    complete(handlerFailure(request, fault));
  }
}

}  // namespace qingying
