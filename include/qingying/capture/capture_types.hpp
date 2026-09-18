#pragma once

#include "qingying/action/common_types.hpp"
#include "qingying/geometry/rect_types.h"
#include "qingying/window/window_query_types.hpp"
#include <chrono>
#include <optional>
#include <string>

namespace qingying {

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

struct CaptureRegionRequest {
  ScreenPhysicalRect region{};
};

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

}  // namespace qingying
