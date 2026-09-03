#include "qingying/app/longshot_controller.hpp"

#include "qingying/app/app_messages.hpp"
#include "qingying/app/ui_message_channel.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <memory>
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
    const HWND completion_window = owner_window;
    active.store(true);

    try {
      worker = std::thread([this, request, completion_window] {
        Image image;
        const ActionResult result = engine.captureSelection(
            request, image,
            [this](const Image& preview) {
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
      active.store(false);
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
  }

  void join() noexcept {
    if (worker.joinable()) {
      worker.join();
    }
    active.store(false);
  }

  void shutdown() noexcept {
    if (shutting_down.exchange(true)) {
      return;
    }
    cancel();
    join();
    messages.drain();
    owner_window = nullptr;
  }

  bool activeState() const noexcept { return active.load(); }

  void drainMessages() noexcept { messages.drain(); }

 private:
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
  UiMessageChannel messages;
  std::atomic<bool> stop_requested{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> shutting_down{false};
  std::atomic<bool> active{false};
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

void LongShotController::shutdown() noexcept { impl_->shutdown(); }

void LongShotController::drainMessages() noexcept { impl_->drainMessages(); }

bool LongShotController::active() const noexcept {
  return impl_->activeState();
}

}  // namespace qingying
