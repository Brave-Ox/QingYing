#include "qingying/app/longshot_controller.hpp"

#include "qingying/app/app_messages.hpp"
#include "qingying/app/longshot_limits_provider.hpp"
#include "qingying/app/ui_message_channel.h"
#include "qingying/diagnostics/fault_boundary.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace qingying {

struct LongShotController::Impl {
  Impl(LongShotEngine& engine_in, SelectionOverlay& overlay_in,
       LongShotLimitsProvider& limits_provider_in)
      : engine(engine_in),
        overlay(overlay_in),
        limits_provider(limits_provider_in)
  {
  }

  void setOwnerWindow(HWND window) noexcept { owner_window = window; }

  bool start(const LongShotRequest& request) {
    cancel();
    join();
    messages.drain();
    if (shutting_down.load() || !request.valid() || owner_window == nullptr) {
      return false;
    }
    const LongShotLimits limits = limits_provider.snapshot();

    stop_requested.store(false);
    cancelled.store(false);
    paused.store(false);
    const std::uint64_t diagnostic_request_id = next_request_id.fetch_add(1);
    active_request_id.store(diagnostic_request_id);
    current_session.store(diagnostic_request_id);
    overlay.setLongShotSession(diagnostic_request_id);
    progress_frames.store(0);
    const HWND completion_window = owner_window;
    active.store(true);
    {
      std::lock_guard<std::mutex> lock(worker_mutex);
      worker_done = false;
    }

    try {
      auto context = currentFaultContext();
      if (!context.request_id) context.request_id = diagnostic_request_id;
      worker = std::thread([this, request, limits, completion_window, context,
                            diagnostic_request_id] {
        DiagnosticScope scope(context);
        struct WorkerDone final {
          Impl* owner;
          ~WorkerDone() { owner->markWorkerDone(); }
        } done{this};
        LongShotOutcome outcome;
        FaultDiagnostic fault;
        if (!containFault(FaultOrigin::Worker, FaultDomain::Request, [&] {
          engine.captureSelection(
            request, outcome,
            [this, diagnostic_request_id](const Image& preview) {
              if (cancelled.load() || shutting_down.load() ||
                  current_session.load() != diagnostic_request_id) return;
              progress_frames.fetch_add(1);
              (void)overlay.postLongShotPreview(preview, diagnostic_request_id);
            },
            [this] {
              std::unique_lock<std::mutex> lock(worker_mutex);
              control_condition.wait(lock, [this] {
                return !paused.load() || stop_requested.load();
              });
              return !stop_requested.load();
            }, limits);
        }, &fault)) {
          outcome.stop_reason = LongShotStopReason::CaptureFailed;
          outcome.diagnostic.ok = false;
          outcome.diagnostic.error_code = fault.error_code;
          outcome.diagnostic.diagnostic = fault;
          outcome.diagnostic.failure_stage = "longshot_worker";
        }

        if (cancelled.load()) {
          outcome = cancelledOutcome();
        }

        if (shutting_down.load()) {
          return;
        }

        if (!containFault(FaultOrigin::Worker, FaultDomain::Request, [&] {
          LongShotCompletionMessage completion;
          completion.session_id = diagnostic_request_id;
          completion.outcome = std::move(outcome);
          completion.outcome.image.classifyMemory(ImageMemoryKind::WorkerQueue);
          const std::optional<UiMessageToken> token =
              messages.push(std::move(completion));
          if (!token.has_value()) {
            postCompletionFailure(completion_window, diagnostic_request_id);
            return;
          }
          if (!PostMessageW(completion_window, WM_QINGYING_LONGSHOT_COMPLETE, 0,
                            static_cast<LPARAM>(*token))) {
            messages.discard(*token);
            return;
          }
        })) {
          containFault(FaultOrigin::Worker, FaultDomain::Request,
                       [&] { postCompletionFailure(completion_window,
                                                   diagnostic_request_id); });
        }
      });
    } catch (...) {
      markWorkerDone();
      active.store(false);
      active_request_id.store(0);
      stop_requested.store(true);
      paused.store(false);
      current_session.store(0);
      overlay.setLongShotSession(0);
      return false;
    }
    return true;
  }

  void handleControl(LongShotControl control) noexcept {
    {
      std::lock_guard<std::mutex> lock(worker_mutex);
      if (control == LongShotControl::TogglePause) {
        paused.store(!paused.load());
      } else if (control == LongShotControl::Stop) {
        stop_requested.store(true);
        paused.store(false);
      }
      control_condition.notify_all();
    }
    engine.notifyControlChange();
  }

  bool handleCompletion(UiMessageToken token, ActionResult& result,
                        Image& image) {
    LongShotOutcome outcome;
    if (!handleCompletion(token, outcome)) return false;
    result = std::move(outcome.diagnostic);
    image = result.ok ? std::move(outcome.image) : Image{};
    return true;
  }

  bool handleCompletion(UiMessageToken token, LongShotOutcome& outcome) {
    auto completion =
        messages.take<LongShotCompletionMessage>(token);
    if (shutting_down.load() || !completion.has_value() ||
        completion->session_id != current_session.load()) {
      return false;
    }
    join();
    outcome = cancelled.load() ? cancelledOutcome()
                               : std::move(completion->outcome);
    current_session.store(0);
    // 完成后允许同会话的最后一份预览显示；新会话/取消会将它清理。
    return true;
  }

  void cancel() noexcept {
    {
      std::lock_guard<std::mutex> lock(worker_mutex);
      cancelled.store(true);
      stop_requested.store(true);
      paused.store(false);
    }
    control_condition.notify_all();
    overlay.setLongShotSession(0);
    engine.cancel();
  }

  void join() noexcept {
    (void)joinUntil((std::chrono::steady_clock::time_point::max)());
  }

  void beginShutdown() noexcept {
    shutting_down.store(true);
    cancel();
  }

  bool joinUntil(std::chrono::steady_clock::time_point deadline) noexcept {
    if (!worker.joinable()) {
      active.store(false);
      return true;
    }
    {
      std::unique_lock<std::mutex> lock(worker_mutex);
      if (!worker_done_condition.wait_until(lock, deadline,
                                            [this] { return worker_done; })) {
        return false;
      }
    }
    if (worker.get_id() == std::this_thread::get_id()) return false;
    worker.join();
    active.store(false);
    active_request_id.store(0);
    return true;
  }

  void finishShutdown() noexcept {
    if (worker.joinable()) return;
    messages.drain();
    owner_window = nullptr;
  }

  void shutdown() noexcept {
    beginShutdown();
    if (joinUntil((std::chrono::steady_clock::time_point::max)())) {
      finishShutdown();
    }
  }

  bool activeState() const noexcept { return active.load(); }

  std::string diagnosticSnapshot() const {
    return "thread=longshot_worker request_id=" +
           std::to_string(active_request_id.load()) + " plugin_id=" +
           engine.activeProfileName() + " queue_length=0 last_progress=frame_" +
           std::to_string(progress_frames.load());
  }

  void drainMessages() noexcept {
    messages.drain();
    current_session.store(0);
    overlay.setLongShotSession(0);
  }

 private:
  void markWorkerDone() noexcept {
    {
      std::lock_guard<std::mutex> lock(worker_mutex);
      worker_done = true;
    }
    active.store(false);
    worker_done_condition.notify_all();
  }

  static LongShotOutcome cancelledOutcome() {
    LongShotOutcome outcome;
    outcome.stop_reason = LongShotStopReason::Cancelled;
    outcome.diagnostic.error_code = ErrorCode::kCancelled;
    outcome.diagnostic.message = "longshot: capture cancelled";
    return outcome;
  }

  void postCompletionFailure(HWND completion_window, std::uint64_t session) {
    LongShotCompletionMessage completion;
    completion.session_id = session;
    completion.outcome.stop_reason = LongShotStopReason::CaptureFailed;
    completion.outcome.diagnostic.error_code = ErrorCode::kCaptureFailed;
    completion.outcome.diagnostic.failure_stage = "longshot_completion";
    const auto token = messages.push(std::move(completion));
    if (token && !PostMessageW(completion_window, WM_QINGYING_LONGSHOT_COMPLETE,
                               0, static_cast<LPARAM>(*token)))
      messages.discard(*token);
  }

  LongShotEngine& engine;
  SelectionOverlay& overlay;
  LongShotLimitsProvider& limits_provider;
  HWND owner_window{nullptr};
  std::thread worker;
  std::mutex worker_mutex;
  std::condition_variable worker_done_condition;
  std::condition_variable control_condition;
  bool worker_done{true};
  UiMessageChannel messages;
  std::atomic<bool> stop_requested{false};
  std::atomic<bool> cancelled{false};
  std::atomic<std::uint64_t> current_session{0};
  std::atomic<bool> paused{false};
  std::atomic<bool> shutting_down{false};
  std::atomic<bool> active{false};
  std::atomic<std::uint64_t> next_request_id{1};
  std::atomic<std::uint64_t> active_request_id{0};
  std::atomic<std::size_t> progress_frames{0};
};

LongShotController::LongShotController(LongShotEngine& engine,
                                       SelectionOverlay& overlay,
                                       LongShotLimitsProvider& limits_provider)
    : impl_(std::make_unique<Impl>(engine, overlay, limits_provider))
{
}

LongShotController::~LongShotController() { impl_->shutdown(); }

void LongShotController::setOwnerWindow(HWND owner_window) noexcept {
  impl_->setOwnerWindow(owner_window);
}

bool LongShotController::start(const LongShotRequest& request) {
  return impl_->start(request);
}

void LongShotController::handleControl(LongShotControl control) noexcept {
  impl_->handleControl(control);
}

bool LongShotController::handleCompletion(UiMessageToken token,
                                          ActionResult& result,
                                          Image& image) {
  return impl_->handleCompletion(token, result, image);
}

bool LongShotController::handleCompletion(UiMessageToken token,
                                          LongShotOutcome& outcome) {
  return impl_->handleCompletion(token, outcome);
}

void LongShotController::cancel() noexcept { impl_->cancel(); }

void LongShotController::join() noexcept { impl_->join(); }

void LongShotController::beginShutdown() noexcept {
  impl_->beginShutdown();
}

bool LongShotController::joinUntil(
    std::chrono::steady_clock::time_point deadline) noexcept {
  return impl_->joinUntil(deadline);
}

void LongShotController::finishShutdown() noexcept {
  impl_->finishShutdown();
}

void LongShotController::shutdown() noexcept { impl_->shutdown(); }

void LongShotController::drainMessages() noexcept { impl_->drainMessages(); }

bool LongShotController::active() const noexcept {
  return impl_->activeState();
}

std::string LongShotController::diagnosticSnapshot() const {
  return impl_->diagnosticSnapshot();
}

}  // namespace qingying
