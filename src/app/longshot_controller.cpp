#include "qingying/app/longshot_controller.hpp"

#include "qingying/app/app_messages.hpp"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <new>
#include <thread>
#include <utility>

namespace qingying {

namespace {

struct LongShotCompletion {
  ActionResult result;
  Image image;
};

}  // namespace

struct LongShotController::Impl {
  Impl(LongShotEngine& engine_in, SelectionOverlay& overlay_in)
      : engine(engine_in), overlay(overlay_in) {}

  void setOwnerWindow(std::uintptr_t window) noexcept {
    owner_window = reinterpret_cast<HWND>(window);
  }

  bool start(const LongShotRequest& request) {
    cancel();
    join();
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

        std::unique_ptr<LongShotCompletion> completion(
            new (std::nothrow) LongShotCompletion);
        if (completion == nullptr) {
          postCompletionFailure(completion_window);
          return;
        }
        completion->result = result;
        completion->image = std::move(image);
        if (!PostMessageW(completion_window, WM_QINGYING_LONGSHOT_COMPLETE, 0,
                          reinterpret_cast<LPARAM>(completion.get()))) {
          (void)overlay.postLongShotFinished(false);
          return;
        }
        (void)completion.release();
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

  bool handleCompletion(std::intptr_t payload, ActionResult& result,
                        Image& image) {
    std::unique_ptr<LongShotCompletion> completion(
        reinterpret_cast<LongShotCompletion*>(payload));
    join();
    if (shutting_down.load() || completion == nullptr) {
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
    owner_window = nullptr;
  }

  bool activeState() const noexcept { return active.load(); }

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
  std::atomic<bool> stop_requested{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> shutting_down{false};
  std::atomic<bool> active{false};
};

LongShotController::LongShotController(LongShotEngine& engine,
                                       SelectionOverlay& overlay)
    : impl_(std::make_unique<Impl>(engine, overlay)) {}

LongShotController::~LongShotController() { impl_->shutdown(); }

void LongShotController::setOwnerWindow(
    std::uintptr_t owner_window) noexcept {
  impl_->setOwnerWindow(owner_window);
}

bool LongShotController::start(const LongShotRequest& request) {
  return impl_->start(request);
}

void LongShotController::handleControl(LongShotControl control) noexcept {
  impl_->handleControl(control);
}

bool LongShotController::handleCompletion(std::intptr_t payload,
                                          ActionResult& result,
                                          Image& image) {
  return impl_->handleCompletion(payload, result, image);
}

void LongShotController::cancel() noexcept { impl_->cancel(); }

void LongShotController::join() noexcept { impl_->join(); }

void LongShotController::shutdown() noexcept { impl_->shutdown(); }

bool LongShotController::active() const noexcept {
  return impl_->activeState();
}

}  // namespace qingying
