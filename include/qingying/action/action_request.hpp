#pragma once

#include "qingying/capture/capture_types.hpp"
#include "qingying/automation/status_request.hpp"
#include "qingying/operation/operation_types.hpp"
#include <chrono>
#include <memory>
#include <utility>
#include <variant>

namespace qingying {

using ActionPayload =
    std::variant<StatusRequest, CaptureRegionRequest, CaptureWindowRequest,
                 CropCenterRequest, CopyRequest, SaveRequest, PinRequest>;

inline ActionType actionType(const StatusRequest&) noexcept {
  return ActionType::Status;
}

inline ActionType actionType(const CaptureRegionRequest&) noexcept {
  return ActionType::CaptureRegion;
}

inline ActionType actionType(const CaptureWindowRequest&) noexcept {
  return ActionType::CaptureWindow;
}

inline ActionType actionType(const CropCenterRequest&) noexcept {
  return ActionType::CropCenter;
}

inline ActionType actionType(const CopyRequest&) noexcept {
  return ActionType::Copy;
}

inline ActionType actionType(const SaveRequest&) noexcept {
  return ActionType::Save;
}

inline ActionType actionType(const PinRequest&) noexcept {
  return ActionType::Pin;
}

inline ActionType actionType(const ActionPayload& payload) noexcept {
  return std::visit(
      [](const auto& request) noexcept { return actionType(request); }, payload);
}

struct ActionRequest {
  RequestId request_id{kInvalidRequestId};
  OperationId operation_id{kInvalidOperationId};
  CancellationToken cancellation;
  std::chrono::milliseconds timeout{0};
  std::chrono::steady_clock::time_point submitted_at{
      std::chrono::steady_clock::now()};
  ActionPayload payload{StatusRequest{}};
  ActionContext context{};
  // Trusted admission control, never populated from external payload fields.
  std::shared_ptr<IOperationControl> operation_control;

  ActionRequest() = default;

  explicit ActionRequest(ActionPayload payload_in)
      : payload(std::move(payload_in)) {}

  ActionType type() const noexcept { return actionType(payload); }

  void markSubmitted() noexcept {
    submitted_at = std::chrono::steady_clock::now();
  }

  bool timedOut() const noexcept {
    return timeout.count() > 0 &&
           std::chrono::steady_clock::now() >= submitted_at + timeout;
  }
};

template <typename Payload>
ActionRequest makeActionRequest(Payload payload) {
  return ActionRequest{ActionPayload{std::move(payload)}};
}

ActionValidationResult validateActionRequest(const ActionRequest& request);

}  // namespace qingying
