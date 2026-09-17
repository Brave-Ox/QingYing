#include "qingying/app/capture_service.h"

#include "qingying/app/result_store.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/automation/automation_contract.h"
#include "qingying/pin/pin_manager.hpp"

#include <Windows.h>

#include "qingying/app/export_executor.h"
#include "qingying/app/app_messages.hpp"
#include <atomic>
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

// The worker only owns Work. UI guards and reservations stay in Pending,
// so cancellation can never destroy PinManager resources on the worker thread.
struct CaptureService::AsyncImpl {
  struct Work {
    ActionRequest request;
    ScreenPhysicalRect region;
    ActionResult result;
    Image image;
    ImageTransform transform;
    std::optional<ResolvedWindow> window;
    RECT native_bounds{};
    std::atomic_bool done{false};
  };
  struct Pending {
    std::shared_ptr<Work> work;
    InteractionGate::Guard interaction;
    ResultBudget::Reservation reservation;
    std::unique_ptr<PinManager::CaptureGuard> pins;
    ImageCompletion completion;
    bool resolve_only{false};
  };
  CaptureService& service;
  ExportExecutor executor{1, "capture_worker"};
  HWND window{nullptr};
  std::unique_ptr<Pending> pending;
  const InteractionGate::Guard* settling_owner{nullptr};
  std::optional<ResolvedWindow> settling_window;

  explicit AsyncImpl(CaptureService& owner) : service(owner) {}
  ~AsyncImpl() {
    executor.shutdown();
    try { settle(true); } catch (...) {}
    if (window) DestroyWindow(window);
  }
  static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) noexcept {
    auto* self = reinterpret_cast<AsyncImpl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
      self = static_cast<AsyncImpl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (self && (message == WM_QINGYING_CAPTURE_COMPLETE || message == WM_TIMER)) {
      try { self->settle(false); } catch (...) {
        OutputDebugStringW(L"QingYing capture completion callback failed\n");
      }
      return 0;
    }
    return DefWindowProcW(hwnd, message, w, l);
  }
  bool ensureWindow() {
    if (window) return true;
    WNDCLASSW cls{};
    cls.lpfnWndProc = procedure;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"QingYing.CaptureExecutor.Completion";
    if (!RegisterClassW(&cls) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    window = CreateWindowExW(0, cls.lpszClassName, L"", 0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, cls.hInstance, this);
    return window != nullptr;
  }
  static int aborted(const ActionRequest& request) {
    if (request.timedOut()) return ErrorCode::kTimeout;
    if (request.cancellation.isCancellationRequested()) return ErrorCode::kCancelled;
    if (request.operation_control) {
      auto status = request.operation_control->status();
      if (status.abort_reason == AbortReason::Deadline) return ErrorCode::kTimeout;
      if (status.abort_reason != AbortReason::None) return ErrorCode::kCancelled;
    }
    return ErrorCode::kOk;
  }
  void settle(bool stopping) {
    if (!pending) return;
    auto work = pending->work;
    const int abort = stopping || executor.stopping() ? ErrorCode::kShuttingDown
                                                     : aborted(work->request);
    if (!work->done.load(std::memory_order_acquire) && abort == ErrorCode::kOk) return;
    if (abort == ErrorCode::kOk && executor.running()) return;
    auto finished = std::move(pending);
    KillTimer(window, 1);
    finished->pins.reset();
    finished->reservation = {};
    // No worker-written fields are read until done is acquired.
    if (abort != ErrorCode::kOk) {
      finished->completion(failure(abort, "capture stopped before submission"), {});
      return;
    }
    if (!finished->resolve_only && work->window && work->result.ok) {
      const HWND target = reinterpret_cast<HWND>(work->window->identity.native_handle);
      DWORD pid = 0;
      RECT bounds{};
      GetWindowThreadProcessId(target, &pid);
      const auto& expected = work->native_bounds;
      if (!IsWindow(target) || pid != work->window->identity.process_id ||
          !GetWindowRect(target, &bounds) || bounds.left != expected.left ||
          bounds.top != expected.top || bounds.right != expected.right ||
          bounds.bottom != expected.bottom)
        work->result = failure(ErrorCode::kWindowNotFound, "window changed during capture");
    }
    settling_window = work->window;
    settling_owner = &finished->interaction;
    struct Reset { AsyncImpl& self; ~Reset() { self.settling_owner = nullptr; self.settling_window.reset(); } } reset{*this};
    finished->completion(std::move(work->result), std::move(work->image));
  }
  void start(const ActionRequest& request, ScreenPhysicalRect region,
             ImageCompletion completion, const InteractionGate::Guard* owner,
             bool resolve_only = false, bool publish_scope = false, ImageTransform transform = {}) {
    if (executor.stopping()) {
      completion(failure(ErrorCode::kShuttingDown, "capture executor is stopping"), {});
      return;
    }
    if (pending || executor.running() || executor.queued()) {
      completion(failure(ErrorCode::kBusy, "capture executor is occupied"), {});
      return;
    }
    if (!request.context.valid() || (!resolve_only && !service.validRegion(region))) {
      completion(failure(ErrorCode::kInvalidArgument, "invalid capture request"), {});
      return;
    }
    if (const int abort = aborted(request); abort != ErrorCode::kOk) {
      completion(failure(abort, "capture aborted before admission"), {});
      return;
    }
    auto interaction = service.gate_.acquire(InteractionKind::Capture, owner);
    if (!interaction) {
      completion(failure(ErrorCode::kBusy, "another GUI interaction is active"), {});
      return;
    }
    auto state = std::make_unique<Pending>();
    if (!resolve_only) {
      state->reservation = service.results_.reserve(request.context.result_scope,
                                                   region.width, region.height);
      if (!state->reservation) {
        completion(failure(ErrorCode::kResourceLimit, "capture exceeds result budget"), {});
        return;
      }
    }
    if (!ensureWindow() || !SetTimer(window, 1, 16, nullptr)) {
      completion(failure(ErrorCode::kCaptureFailed, "capture completion window unavailable"), {});
      return;
    }
    auto work = std::make_shared<Work>();
    work->request = request;
    work->region = region;
    work->transform = std::move(transform);
    if (!resolve_only) work->window = settling_window;
    state->work = work;
    state->interaction = std::move(interaction);
    state->completion = std::move(completion);
    state->resolve_only = resolve_only;
    if (!resolve_only) {
      if (publish_scope) service.results_.clearScope(request.context.result_scope);
      state->pins = std::make_unique<PinManager::CaptureGuard>(service.pins_);
    }
    pending = std::move(state);
    const HWND notify = window;
    auto* capture = &service;
    const bool accepted = executor.submit([work, capture, notify, resolve_only] {
      try {
        const int abort = aborted(work->request);
        if (abort != ErrorCode::kOk) {
          work->result = failure(abort, "capture aborted before acquisition");
        } else if (resolve_only) {
          const auto& query = std::get<CaptureWindowRequest>(work->request.payload);
          auto resolved = capture->window_resolver_.resolve(WindowQuery{
              query.window_query, query.match == WindowMatchMode::Exact
                  ? WindowTitleMatch::Exact : WindowTitleMatch::Contains, query.process_id});
          work->result = failure(resolved.error_code, "window query failed");
          work->result.output = resolved.candidates;
          if (resolved.ok() && resolved.window &&
              capture->window_resolver_.revalidate(*resolved.window)) {
            work->window = resolved.window;
            work->result.ok = true;
          } else if (resolved.ok()) {
            work->result = failure(ErrorCode::kWindowNotFound, "window changed during discovery");
          }
        } else if (work->window && !capture->window_resolver_.revalidate(*work->window)) {
          work->result = failure(ErrorCode::kWindowNotFound, "window changed before capture");
        } else {
          if (work->window)
            GetWindowRect(reinterpret_cast<HWND>(work->window->identity.native_handle),
                          &work->native_bounds);
          work->result = capture->capture_invoker_(work->region, work->image);
          if (work->result.ok && work->transform && aborted(work->request) == ErrorCode::kOk)
            work->transform(work->image);
        }
      } catch (...) {
        work->result = failure(ErrorCode::kCaptureFailed, "capture provider threw an exception");
      }
      work->done.store(true, std::memory_order_release);
      PostMessageW(notify, WM_QINGYING_CAPTURE_COMPLETE, 0, 0);
    }, {}, request.request_id, resolve_only ? "window_discovery" : "capture");
    if (!accepted) {
      work->result = failure(ErrorCode::kResourceLimit, "capture queue is full");
      work->done.store(true, std::memory_order_release);
      settle(false);
    }
  }
};

CaptureService::CaptureService(
    CaptureEngine& capture, ResultStore& results, PinManager& pins,
    InteractionGate& gate, CaptureInvoker capture_invoker,
    PrimaryMonitorProvider primary_monitor,
    WindowResolver::Catalog window_catalog)
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
                                             primaryMonitorRect}),
      window_resolver_(std::move(window_catalog)),
      async_(std::make_unique<AsyncImpl>(*this)) {}

CaptureService::~CaptureService() = default;

void CaptureService::captureImageAsync(const ActionRequest& request,
    const ScreenPhysicalRect& region, ImageCompletion completion,
    const InteractionGate::Guard* owner, ImageTransform transform) {
  checkThread();
  async_->start(request, region, std::move(completion), owner, false, false, std::move(transform));
}

void CaptureService::captureAsync(const ActionRequest& request,
    const ScreenPhysicalRect& region, ActionCompletion completion,
    const InteractionGate::Guard* owner) {
  checkThread();
  async_->start(request, region,
      [this, request, region, completion = std::move(completion)](ActionResult result, Image image) mutable {
        if (result.ok && AsyncImpl::aborted(request) != ErrorCode::kOk)
          result = failure(AsyncImpl::aborted(request), "capture aborted before publication");
        if (result.ok && (image.width != region.width || image.height != region.height))
          result = failure(ErrorCode::kCaptureFailed, "capture image dimensions changed");
        if (result.ok) {
          auto reservation = results_.reserve(request.context.result_scope, image.width, image.height);
          if (!reservation) {
            result = failure(ErrorCode::kResourceLimit, "capture publication exceeds budget");
          } else if (request.operation_control && !request.operation_control->tryCommit()) {
            result = failure(request.timedOut() ? ErrorCode::kTimeout : ErrorCode::kCancelled,
                             "capture cancelled before publication");
          } else {
            const auto id = results_.publish(request.context.result_scope, std::move(image),
                                            std::move(reservation), region);
            const auto lease = results_.acquire(request.context.result_scope, id);
            if (lease) result.output = lease.metadata();
            else result = failure(ErrorCode::kCaptureFailed, "capture returned invalid image");
          }
        }
        if (!result.ok) result.output = std::monostate{};
        completion(std::move(result));
      }, owner, false, true);
}

void CaptureService::captureActionAsync(const ActionRequest& request, ActionCompletion completion) {
  checkThread();
  if (const auto* region = std::get_if<CaptureRegionRequest>(&request.payload)) {
    captureAsync(request, region->region, std::move(completion));
  } else if (const auto* crop = std::get_if<CropCenterRequest>(&request.payload)) {
    auto centered = centeredRect(primary_monitor_(), crop->width, crop->height);
    if (centered) captureAsync(request, *centered, std::move(completion));
    else completion(failure(ErrorCode::kInvalidArgument, "invalid centered crop dimensions"));
  } else if (std::holds_alternative<CaptureWindowRequest>(request.payload)) {
    async_->start(request, {}, [this, request, completion = std::move(completion)]
        (ActionResult result, Image) mutable {
      // settle retains the discovery guard throughout this callback. Its shared
      // ownership authorizes the nested capture without reopening admission.
      if (!result.ok) { completion(std::move(result)); return; }
      // Resolution metadata is conveyed independently of the external output.
      const auto& candidates = std::get<WindowCandidates>(result.output);
      if (candidates.candidates.size() != 1) {
        completion(failure(ErrorCode::kWindowNotFound, "window changed before capture"));
        return;
      }
      captureAsync(request, candidates.candidates.front().bounds, std::move(completion),
                   async_->settling_owner);
    }, nullptr, true);
  } else completion(failure(ErrorCode::kInvalidArgument, "invalid capture payload"));
}

void CaptureService::beginStop() noexcept {
  async_->executor.requestStop();
  try { async_->settle(true); } catch (...) {}
}
bool CaptureService::joinUntil(std::chrono::steady_clock::time_point deadline) noexcept {
  return async_->executor.joinUntil(deadline);
}
std::string CaptureService::diagnosticSnapshot() const {
  return async_->executor.diagnosticSnapshot();
}

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

ActionResult CaptureService::captureWindow(
    const ActionRequest& request, const CaptureWindowRequest& window,
    const InteractionGate::Guard* interaction_owner) {
  checkThread();
  const auto resolved = window_resolver_.resolve(WindowQuery{
      window.window_query,
      window.match == WindowMatchMode::Exact ? WindowTitleMatch::Exact
                                             : WindowTitleMatch::Contains,
      window.process_id});
  if (!resolved.ok() || !resolved.window) {
    auto result = failure(resolved.error_code,
        resolved.error_code == ErrorCode::kWindowAmbiguous
            ? "window query is ambiguous" : "window was not found");
    result.output = resolved.candidates;
    return result;
  }
  ResolvedWindow current;
  if (!window_resolver_.revalidate(*resolved.window, &current))
    return failure(ErrorCode::kWindowNotFound,
                   "window changed before capture");
  return capture(request, current.identity.bounds, interaction_owner);
}

}  // namespace qingying
