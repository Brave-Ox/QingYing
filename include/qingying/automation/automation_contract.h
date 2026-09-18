#pragma once

#include "qingying/automation/automation_client.h"

#include <type_traits>

namespace qingying {

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
      [&request, &limits](const auto& payload) -> ActionValidationResult {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, ExecuteActionRequest>) {
          const ActionType type = actionType(payload.payload);
          const bool consumes_result = type == ActionType::Save ||
                                       type == ActionType::Copy ||
                                       type == ActionType::Pin;
          if (payload.result_handle) {
            const bool handle_target =
                (type == ActionType::Save || type == ActionType::Copy ||
                 type == ActionType::Pin) &&
                payload.result_handle->valid() &&
                payload.result_handle->value.size() <= limits.max_opaque_handle_bytes;
            const bool current_target = std::visit([](const auto& action) {
              using Action = std::decay_t<decltype(action)>;
              if constexpr (std::is_same_v<Action, SaveRequest> ||
                            std::is_same_v<Action, CopyRequest> ||
                            std::is_same_v<Action, PinRequest>) {
                return action.result.kind == ResultSelectionKind::Current;
              } else {
                return false;
              }
            }, payload.payload);
            if (!handle_target || !current_target) {
              return {false, "invalid result handle or simultaneous numeric target"};
            }
          }
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
          if (!explicit_selection && !payload.result_handle) {
            return {false, "automation consumption requires an explicit result"};
          }
          // Validate the unchanged ActionPayload before the endpoint allocates
          // its paired internal request/operation IDs and attaches context.
          return validateActionRequest(ActionRequest{payload.payload});
        } else if constexpr (std::is_same_v<Payload, BeginLongShotRequest>) {
          return {true, {}};
        } else if constexpr (std::is_same_v<Payload, GetOperationRequest>) {
          if (payload.operation_handle) return {
              payload.operation_id == 0 && payload.operation_handle->valid() &&
                  payload.operation_handle->value.size() <= limits.max_opaque_handle_bytes,
              "invalid operation handle or simultaneous numeric target"};
          return {payload.operation_id != kInvalidOperationId,
                  payload.operation_id != kInvalidOperationId
                      ? "" : "operation_id must be nonzero"};
        } else if constexpr (std::is_same_v<Payload, CancelOperationRequest>) {
          if (payload.operation_handle) {
            const auto* target = std::get_if<OperationCancellation>(&payload.target);
            return {target && target->operation_id == 0 && payload.operation_handle->valid() &&
                payload.operation_handle->value.size() <= limits.max_opaque_handle_bytes,
                "invalid operation handle or simultaneous numeric target"};
          }
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
          if (payload.result_handle) return {
              payload.result_id == 0 && payload.result_handle->valid() &&
                  payload.result_handle->value.size() <= limits.max_opaque_handle_bytes,
              "invalid result handle or simultaneous numeric target"};
          return {payload.result_id != kInvalidResultId,
                  payload.result_id != kInvalidResultId
                      ? "" : "result_id must be nonzero"};
        }
      }, request.payload);
}

}  // namespace qingying
