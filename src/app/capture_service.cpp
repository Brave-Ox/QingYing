#include "qingying/app/capture_service.h"

#include "qingying/app/result_store.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/automation/automation_contract.h"
#include "qingying/pin/pin_manager.hpp"

#include <Windows.h>

#include <limits>
#include <stdexcept>
#include <utility>

namespace qingying {
namespace {

ActionResult failure(int code, const char* message) {
  ActionResult result;
  result.error_code = code;
  result.message = message;
  return result;
}

ScreenPhysicalRect primaryMonitorRect() noexcept {
  const POINT origin{};
  const HMONITOR monitor =
      MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) return {};
  const auto width = static_cast<std::int64_t>(info.rcMonitor.right) -
                     info.rcMonitor.left;
  const auto height = static_cast<std::int64_t>(info.rcMonitor.bottom) -
                      info.rcMonitor.top;
  if (width <= 0 || height <= 0 ||
      width > (std::numeric_limits<int>::max)() ||
      height > (std::numeric_limits<int>::max)()) return {};
  return {info.rcMonitor.left, info.rcMonitor.top,
          static_cast<int>(width), static_cast<int>(height)};
}

}  // namespace

CaptureService::CaptureService(
    CaptureEngine& capture, ResultStore& results, PinManager& pins,
    InteractionGate& gate, CaptureInvoker capture_invoker,
    PrimaryMonitorProvider primary_monitor)
    : capture_(capture),
      results_(results),
      pins_(pins),
      gate_(gate),
      capture_invoker_(capture_invoker ? std::move(capture_invoker)
                                       : CaptureInvoker{[this](const auto& region,
                                                               Image& image) {
                                           return capture_.captureRegion(region,
                                                                         image);
                                         }}),
      primary_monitor_(primary_monitor ? std::move(primary_monitor)
                                       : PrimaryMonitorProvider{
                                             primaryMonitorRect}) {}

void CaptureService::checkThread() const {
  if (std::this_thread::get_id() != ui_thread_)
    throw std::logic_error("capture service requires UI thread");
}

bool CaptureService::validRegion(const ScreenPhysicalRect& region) noexcept {
  if (region.width <= 0 || region.height <= 0) return false;
  const auto right = static_cast<std::int64_t>(region.x) + region.width;
  const auto bottom = static_cast<std::int64_t>(region.y) + region.height;
  if (right > (std::numeric_limits<int>::max)() ||
      bottom > (std::numeric_limits<int>::max)()) return false;
  const auto row_bytes = static_cast<std::uint64_t>(region.width) *
                         sizeof(std::uint32_t);
  const auto image_bytes = row_bytes *
                           static_cast<std::uint64_t>(region.height);
  return row_bytes <= static_cast<std::uint64_t>((std::numeric_limits<int>::max)()) &&
         image_bytes <= static_cast<std::uint64_t>(
                            (std::numeric_limits<std::size_t>::max)());
}

std::optional<ScreenPhysicalRect> CaptureService::centeredRect(
    const ScreenPhysicalRect& monitor, int width, int height) noexcept {
  if (!validRegion(monitor) || width <= 0 || height <= 0 ||
      width > monitor.width || height > monitor.height) return std::nullopt;
  const auto x = static_cast<std::int64_t>(monitor.x) +
                 (static_cast<std::int64_t>(monitor.width) - width) / 2;
  const auto y = static_cast<std::int64_t>(monitor.y) +
                 (static_cast<std::int64_t>(monitor.height) - height) / 2;
  if (x < (std::numeric_limits<int>::min)() ||
      x > (std::numeric_limits<int>::max)() ||
      y < (std::numeric_limits<int>::min)() ||
      y > (std::numeric_limits<int>::max)()) return std::nullopt;
  ScreenPhysicalRect result{static_cast<int>(x), static_cast<int>(y), width,
                            height};
  return validRegion(result) ? std::optional<ScreenPhysicalRect>{result}
                             : std::nullopt;
}

ActionResult CaptureService::capture(
    const ActionRequest& request, const ScreenPhysicalRect& region,
    const InteractionGate::Guard* interaction_owner) {
  checkThread();
  if (!request.context.valid() || !validRegion(region))
    return failure(ErrorCode::kInvalidArgument,
                   "capture region is outside integer bounds");

  auto interaction = gate_.acquire(InteractionKind::Capture,
                                   interaction_owner);
  if (!interaction)
    return failure(ErrorCode::kBusy, "another GUI interaction is active");

  auto reservation = results_.reserve(request.context.result_scope,
                                      region.width, region.height);
  if (!reservation)
    return failure(ErrorCode::kResourceLimit,
                   "capture exceeds the result budget");

  // From this point the new capture is accepted. A later capture/publication
  // failure deliberately leaves this scope empty.
  results_.clearScope(request.context.result_scope);
  Image image;
  ActionResult result;
  {
    auto pin_capture_guard = pins_.temporarilyHideForCapture();
    result = capture_invoker_(region, image);
  }
  if (!result.ok) {
    result.output = std::monostate{};
    return result;
  }

  if (request.operation_control && !request.operation_control->tryCommit()) {
    return failure(request.timedOut() ? ErrorCode::kTimeout
                                     : ErrorCode::kCancelled,
                   "capture cancelled before publication");
  }

  const ResultId id = results_.publish(request.context.result_scope,
      std::move(image), std::move(reservation), region);
  const auto retained = results_.acquire(request.context.result_scope, id);
  if (id == kInvalidResultId || !retained) {
    return failure(ErrorCode::kCaptureFailed,
                   "capture returned an invalid image");
  }
  result.output = retained.metadata();
  return result;
}

ActionResult CaptureService::cropCenter(
    const ActionRequest& request, int width, int height,
    const InteractionGate::Guard* interaction_owner) {
  checkThread();
  const auto monitor = primary_monitor_();
  if (monitor.empty())
    return failure(ErrorCode::kCaptureFailed,
                   "primary monitor bounds are unavailable");
  if (!validRegion(monitor))
    return failure(ErrorCode::kInvalidArgument,
                   "primary monitor bounds overflow");
  const auto region = centeredRect(monitor, width, height);
  if (!region)
    return failure(ErrorCode::kInvalidArgument,
                   "crop dimensions exceed the primary monitor");
  return capture(request, *region, interaction_owner);
}

}  // namespace qingying
