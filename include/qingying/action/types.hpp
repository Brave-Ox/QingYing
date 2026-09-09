#pragma once

#include "qingying/action/automation_limits.h"
#include "qingying/geometry/rect_types.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace qingying {

using ResultId = std::uint64_t;

constexpr ResultId kInvalidResultId = 0;

using ResultScopeId = std::uint64_t;
using PinId = std::uint64_t;

constexpr ResultScopeId kInvalidResultScopeId = 0;
constexpr ResultScopeId kGuiResultScopeId = 1;
constexpr PinId kInvalidPinId = 0;

// The trusted application entry point supplies the scope. It is not a
// client-controlled action argument; existing GUI callers keep their scope.
struct ActionContext {
  ResultScopeId result_scope{kGuiResultScopeId};

  constexpr bool valid() const noexcept {
    return result_scope != kInvalidResultScopeId;
  }
};

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
constexpr int kBusy = 82;
constexpr int kResultExpired = 83;
constexpr int kResultNotFound = 84;
constexpr int kAccessDenied = 85;
constexpr int kResourceLimit = 86;
constexpr int kShuttingDown = 87;
constexpr int kOperationNotFound = 88;
constexpr int kConflict = 89;
constexpr int kNotImplemented = 100;
}  // namespace ErrorCode

// Stable symbols shared by protocol adapters and diagnostics. Unknown numeric
// codes retain their number and use the Unknown symbol.
constexpr std::string_view errorCodeSymbol(int error_code) noexcept {
  switch (error_code) {
    case ErrorCode::kOk: return "Ok";
    case ErrorCode::kUnknown: return "Unknown";
    case ErrorCode::kNotReady: return "NotReady";
    case ErrorCode::kInvalidArgument: return "InvalidArgument";
    case ErrorCode::kCaptureFailed: return "CaptureFailed";
    case ErrorCode::kWindowNotFound: return "WindowNotFound";
    case ErrorCode::kWindowAmbiguous: return "WindowAmbiguous";
    case ErrorCode::kLongShotUnsupported: return "LongShotUnsupported";
    case ErrorCode::kExportFailed: return "ExportFailed";
    case ErrorCode::kCommandUnmatched: return "CommandUnmatched";
    case ErrorCode::kCancelled: return "Cancelled";
    case ErrorCode::kTimeout: return "Timeout";
    case ErrorCode::kBusy: return "Busy";
    case ErrorCode::kResultExpired: return "ResultExpired";
    case ErrorCode::kResultNotFound: return "ResultNotFound";
    case ErrorCode::kAccessDenied: return "AccessDenied";
    case ErrorCode::kResourceLimit: return "ResourceLimit";
    case ErrorCode::kShuttingDown: return "ShuttingDown";
    case ErrorCode::kOperationNotFound: return "OperationNotFound";
    case ErrorCode::kConflict: return "Conflict";
    case ErrorCode::kNotImplemented: return "NotImplemented";
    default: return "Unknown";
  }
}

enum class CaptureMode {
  VisibleScreen,
};

enum class ImageFormat {
  Png,
};

struct CapturedResult {
  ResultId result_id{kInvalidResultId};
  int width{0};
  int height{0};
  ScreenPhysicalRect bounds{};
  CaptureMode capture_mode{CaptureMode::VisibleScreen};
  // GUI results need not expire. External producers provide their actual
  // monotonic deadline; adapters compute remaining time when responding.
  std::optional<std::chrono::steady_clock::time_point> expires_at;
};

struct SavedResult {
  ResultId result_id{kInvalidResultId};
  std::wstring absolute_path;
  ImageFormat format{ImageFormat::Png};
};

struct CopiedResult {
  ResultId result_id{kInvalidResultId};
};

struct PinnedResult {
  ResultId result_id{kInvalidResultId};
  // Metadata only: the pin owner must allocate a real id before publishing.
  PinId pin_id{kInvalidPinId};
};

struct WindowCandidate {
  std::wstring title;
  std::uint32_t process_id{0};
  ScreenPhysicalRect bounds{};
  // An opaque discovery token, never a platform window handle.
  std::string window_token;
};

struct WindowCandidates {
  std::vector<WindowCandidate> candidates;
  bool truncated{false};
};

struct ResourceUsage {
  std::uint64_t result_bytes{0};
  std::uint32_t agent_pin_count{0};
  std::uint64_t agent_pin_bytes{0};
  std::uint64_t reserved_result_bytes{0};
};

struct QueueUsage {
  std::uint32_t queued{0};
  std::uint32_t running{0};
  std::uint32_t ordinary{0};
  std::uint32_t control{0};
};

struct StatusInfo {
  bool reachable{false};
  // Absence means unknown, including when the application cannot be reached.
  std::optional<bool> app_running;
  std::optional<bool> automation_enabled;
  std::optional<bool> busy;
  std::string busy_reason;
  std::string build_version;
  std::string connection_reason;
  std::vector<std::string> capabilities;
  std::optional<AutomationLimits> limits;
  std::optional<ResourceUsage> resources;
  std::optional<QueueUsage> queues;
};

using ActionOutput =
    std::variant<std::monostate, StatusInfo, CapturedResult, SavedResult,
                 CopiedResult, PinnedResult, WindowCandidates>;

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

enum class WindowMatchMode { Contains, Exact };

struct CaptureWindowRequest {
  std::wstring window_query;
  WindowMatchMode match{WindowMatchMode::Contains};
  std::optional<std::uint32_t> process_id;
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
  bool overwrite{false};
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

class IOperationControl;

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
  // Optional diagnostic context for failed operations. Empty/zero means the
  // producer did not expose a more precise failure location.
  std::string failure_stage;
  int failure_frame{0};
  ActionOutput output{};
};

}  // namespace qingying
