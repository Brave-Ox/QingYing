#pragma once

#include <cstdint>
#include <string>

namespace qingying {

enum class ActionType : std::uint32_t {
  Status = 1,
  CaptureRegion = 2,
  CaptureWindow = 3,
  CropCenter = 4,
  Copy = 5,
  Save = 6,
  Pin = 7,
  LongShotForeground = 8,
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
constexpr int kNotImplemented = 100;
}  // namespace ErrorCode

struct ActionRequest {
  ActionType type{ActionType::Status};

  // CaptureRegion / physical pixels
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  // CaptureWindow
  std::wstring window_query;

  // CropCenter
  int crop_w{0};
  int crop_h{0};

  // Save
  std::wstring save_path;
};

struct ActionResult {
  bool ok{false};
  int error_code{ErrorCode::kUnknown};
  std::string message;
  // Optional payload: save path / MCP return value; plain UTF-8 text, no JSON dep.
  std::string data;
};

}  // namespace qingying
