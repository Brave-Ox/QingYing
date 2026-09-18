#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace qingying {

using ResultId = std::uint64_t;

constexpr ResultId kInvalidResultId = 0;

using ResultScopeId = std::uint64_t;
using PinId = std::uint64_t;

constexpr ResultScopeId kInvalidResultScopeId = 0;
constexpr ResultScopeId kGuiResultScopeId = 1;
constexpr PinId kInvalidPinId = 0;

enum class PinSource {
  Gui,
  Agent,
};

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

struct ActionValidationResult {
  bool valid{false};
  std::string message;
};

}  // namespace qingying
