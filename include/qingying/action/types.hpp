#pragma once

#include "qingying/geometry/rect_types.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace qingying {

using ResultId = std::uint64_t;

constexpr ResultId kInvalidResultId = 0;

using RequestId = std::uint64_t;
using OperationId = std::uint64_t;

constexpr RequestId kInvalidRequestId = 0;
constexpr OperationId kInvalidOperationId = 0;

enum class ActionType : std::uint32_t {
  Status = 1,
  CaptureRegion = 2,
  CaptureWindow = 3,
  CropCenter = 4,
  Copy = 5,
  Save = 6,
  Pin = 7,
  LongShotRegion = 8,
  SuggestName = 9,
};

// Stable error codes — see docs/architecture.md §8
namespace ErrorCode {
constexpr int kOk = 0;
constexpr int kUnknown = 1;
constexpr int kNotReady = 10;
constexpr int kInvalidArgument = 20;
constexpr int kCaptureFailed = 30;
constexpr int kWindowNotFound = 40;
constexpr int kWindowAmbiguous = 41;
constexpr int kLongShotUnsupported = 50;
constexpr int kExportFailed = 60;
constexpr int kCommandUnmatched = 70;
constexpr int kCancelled = 80;
constexpr int kTimeout = 81;
constexpr int kNotImplemented = 100;
}  // namespace ErrorCode

enum class ResultSelectionKind {
  Current,
  Explicit,
};

// Identifies which published result an action should consume. The default is
// deliberately current-result semantics for local GUI callers; external
// callers can select a stable result id without touching Session state.
struct ResultSelection {
  ResultSelectionKind kind{ResultSelectionKind::Current};
  ResultId result_id{kInvalidResultId};

  static constexpr ResultSelection current() noexcept { return {}; }

  static constexpr ResultSelection specific(ResultId id) noexcept {
    return ResultSelection{ResultSelectionKind::Explicit, id};
  }

  constexpr bool valid() const noexcept {
    if (kind == ResultSelectionKind::Current) {
      return result_id == kInvalidResultId;
    }
    if (kind == ResultSelectionKind::Explicit) {
      return result_id != kInvalidResultId;
    }
    return false;
  }
};

struct StatusRequest {};

struct CaptureRegionRequest {
  ScreenPhysicalRect region{};
};

struct CaptureWindowRequest {
  std::wstring window_query;
};

struct CropCenterRequest {
  int width{0};
  int height{0};
};

struct CopyRequest {
  ResultSelection result{};
};

struct SaveRequest {
  ResultSelection result{};
  std::wstring path;
};

struct PinRequest {
  ResultSelection result{};
};

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

struct ActionRequest {
  RequestId request_id{kInvalidRequestId};
  OperationId operation_id{kInvalidOperationId};
  CancellationToken cancellation;
  std::chrono::milliseconds timeout{0};
  std::chrono::steady_clock::time_point submitted_at{
      std::chrono::steady_clock::now()};
  ActionPayload payload{StatusRequest{}};

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

// Compatibility boundary for callers that still build the old all-fields
// request. New code must construct ActionRequest from one typed payload.
struct LegacyActionRequest {
  ActionType type{ActionType::Status};

  // CaptureRegion / LongShotRegion, in physical screen pixels.
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  // LongShotRegion: target window recorded before SelectionOverlay takes
  // foreground focus. Kept as an integer so this shared header stays free of
  // Win32 headers.
  std::uintptr_t window_handle{0};

  // CaptureWindow
  std::wstring window_query;

  // CropCenter
  int crop_w{0};
  int crop_h{0};

  // Save
  std::wstring save_path;
};

std::optional<ActionRequest> adaptLegacyActionRequest(
    const LegacyActionRequest& legacy);

struct ActionValidationResult {
  bool valid{false};
  std::string message;
};

ActionValidationResult validateActionRequest(const ActionRequest& request);

struct ActionResult {
  RequestId request_id{kInvalidRequestId};
  OperationId operation_id{kInvalidOperationId};
  bool ok{false};
  int error_code{ErrorCode::kUnknown};
  std::string message;
  // Optional payload: save path / MCP return value; plain UTF-8 text, no JSON dep.
  std::string data;
};

}  // namespace qingying
