#include "qingying/app/longshot_controller.hpp"

#include "qingying/app/app_messages.hpp"
#include "qingying/app/ui_message_channel.h"

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
  Impl(LongShotEngine& engine_in, SelectionOverlay& overlay_in)
      : engine(engine_in), overlay(overlay_in) {}

  void setOwnerWindow(HWND window) noexcept { owner_window = window; }

  bool start(const LongShotRequest& request) {
    cancel();
    join();
    messages.drain();
    if (shutting_down.load() || !request.valid() || owner_window == nullptr) {
      return false;
    }

    stop_requested.store(false);
    paused.store(false);
    const std::uint64_t diagnostic_request_id = next_request_id.fetch_add(1);
    active_request_id.store(diagnostic_request_id);
    progress_frames.store(0);
    const HWND completion_window = owner_window;
    active.store(true);
    {
      std::lock_guard<std::mutex> lock(worker_mutex);
      worker_done = false;
    }

    try {
      worker = std::thread([this, request, completion_window] {
        struct WorkerDone final {
          Impl* owner;
          ~WorkerDone() { owner->markWorkerDone(); }
        } done{this};
        Image image;
        const ActionResult result = engine.captureSelection(
            request, image,
            [this](const Image& preview) {
              progress_frames.fetch_add(1);
              (void)overlay.postLongShotPreview(preview);
            },
            [this] {
              while (paused.load() && !stop_requested.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
              }
              return !stop_requested.load();
            });

        if (shutting_down.load()) {
          return;
        }

        LongShotCompletionMessage completion;
        completion.result = result;
        completion.image = std::move(image);
        const std::optional<UiMessageToken> token =
            messages.push(std::move(completion));
        if (!token.has_value()) {
          postCompletionFailure(completion_window);
          return;
        }
        if (!PostMessageW(completion_window, WM_QINGYING_LONGSHOT_COMPLETE, 0,
                          static_cast<LPARAM>(*token))) {
          messages.discard(*token);
          (void)overlay.postLongShotFinished(false);
          return;
        }
      });
    } catch (...) {
      markWorkerDone();
      active.store(false);
      active_request_id.store(0);
      stop_requested.store(true);
      paused.store(false);
      return false;
    }
    return true;
  }

  void handleControl(LongShotControl control) noexcept {
    if (control == LongShotControl::TogglePause) {
      paused.store(!paused.load());
    } else if (control == LongShotControl::Stop) {
      stop_requested.store(true);
      paused.store(false);
    }
  }

  bool handleCompletion(UiMessageToken token, ActionResult& result,
                        Image& image) {
    auto completion =
        messages.take<LongShotCompletionMessage>(token);
    join();
    if (shutting_down.load() || !completion.has_value()) {
      return false;
    }
    result = std::move(completion->result);
    image = std::move(completion->image);
    return true;
  }

  void cancel() noexcept {
    stop_requested.store(true);
    paused.store(false);
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
      if (!worker_done &&
          worker_done_condition.wait_until(lock, deadline) ==
              std::cv_status::timeout &&
          !worker_done) {
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

  void drainMessages() noexcept { messages.drain(); }

 private:
  void markWorkerDone() noexcept {
    {
      std::lock_guard<std::mutex> lock(worker_mutex);
      worker_done = true;
    }
    active.store(false);
    worker_done_condition.notify_all();
  }

  void postCompletionFailure(HWND completion_window) {
    if (completion_window != nullptr &&
        PostMessageW(completion_window, WM_QINGYING_LONGSHOT_COMPLETE, 0, 0)) {
      return;
    }
    (void)overlay.postLongShotFinished(false);
  }

  LongShotEngine& engine;
  SelectionOverlay& overlay;
  HWND owner_window{nullptr};
  std::thread worker;
  std::mutex worker_mutex;
  std::condition_variable worker_done_condition;
  bool worker_done{true};
  UiMessageChannel messages;
  std::atomic<bool> stop_requested{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> shutting_down{false};
  std::atomic<bool> active{false};
  std::atomic<std::uint64_t> next_request_id{1};
  std::atomic<std::uint64_t> active_request_id{0};
  std::atomic<std::size_t> progress_frames{0};
};

LongShotController::LongShotController(LongShotEngine& engine,
                                       SelectionOverlay& overlay)
    : impl_(std::make_unique<Impl>(engine, overlay)) {}

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
