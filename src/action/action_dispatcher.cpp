#include "qingying/action/action_dispatcher.hpp"

#include <type_traits>

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

}  // namespace

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
          return payload.window_query.empty()
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

std::optional<ActionRequest> adaptLegacyActionRequest(
    const LegacyActionRequest& legacy) {
  switch (legacy.type) {
    case ActionType::Status:
      return makeActionRequest(StatusRequest{});
    case ActionType::CaptureRegion:
      return makeActionRequest(CaptureRegionRequest{
          ScreenPhysicalRect{legacy.x, legacy.y, legacy.width, legacy.height}});
    case ActionType::CaptureWindow:
      return makeActionRequest(CaptureWindowRequest{legacy.window_query});
    case ActionType::CropCenter:
      return makeActionRequest(CropCenterRequest{legacy.crop_w, legacy.crop_h});
    case ActionType::Copy:
      return makeActionRequest(CopyRequest{ResultSelection::current()});
    case ActionType::Save:
      return makeActionRequest(
          SaveRequest{ResultSelection::current(), legacy.save_path});
    case ActionType::Pin:
      return makeActionRequest(PinRequest{ResultSelection::current()});
    case ActionType::LongShotRegion:
    case ActionType::SuggestName:
      // These actions do not have a stable external payload yet. Long-shot
      // continues to use LongShotRequest until its async contract is frozen.
      return std::nullopt;
  }
  return std::nullopt;
}

void ActionDispatcher::registerHandler(std::unique_ptr<IActionHandler> handler) {
  if (!handler) {
    return;
  }
  const Key key = static_cast<Key>(handler->type());
  handlers_[key] = std::move(handler);
}

ActionResult ActionDispatcher::dispatch(const ActionRequest& request) const {
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

  ActionResult result = it->second->handle(request);
  if (request.cancellation.isCancellationRequested()) {
    return requestFailure(request, ErrorCode::kCancelled,
                          "action cancelled during dispatch");
  }
  if (request.timedOut()) {
    return requestFailure(request, ErrorCode::kTimeout,
                          "action timed out during dispatch");
  }
  result.request_id = request.request_id;
  result.operation_id = request.operation_id;
  return result;
}

}  // namespace qingying
