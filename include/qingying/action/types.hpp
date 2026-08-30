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
constexpr int kNotImplemented = 100;
}  // namespace ErrorCode

struct ActionRequest {
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

struct ActionResult {
  bool ok{false};
  int error_code{ErrorCode::kUnknown};
  std::string message;
  // Optional payload: save path / MCP return value; plain UTF-8 text, no JSON dep.
  std::string data;
};

}  // namespace qingying
