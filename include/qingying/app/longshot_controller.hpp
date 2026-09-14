#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/overlay/selection_overlay.hpp"

#include <chrono>
#include <memory>
#include <string>

namespace qingying {

// Owns the asynchronous lifetime around LongShotEngine. The engine remains a
// synchronous capture/profile service; this controller owns the worker,
// pause/stop tokens, cross-thread progress and completion hand-off.
class LongShotController {
 public:
  LongShotController(LongShotEngine& engine, SelectionOverlay& overlay);
  ~LongShotController();

  LongShotController(const LongShotController&) = delete;
  LongShotController& operator=(const LongShotController&) = delete;

  // The tray window receives the completion token. This handle is borrowed
  // from Application and is never destroyed by the controller.
  void setOwnerWindow(HWND owner_window) noexcept;

  // Starts one interactive capture. A second start first requests and joins
  // any previous worker, so at most one session can be active.
  bool start(const LongShotRequest& request);

  // Called by SelectionOverlay's UI callback; it only changes atomics and
  // never joins the worker from inside the overlay event.
  void handleControl(LongShotControl control) noexcept;

  // Takes the WM_QINGYING_LONGSHOT_COMPLETE token, joins the worker, and moves
  // its result into the supplied outputs. A false return means the token was
  // invalid, the payload was already drained, or the controller is shutting
  // down.
  bool handleCompletion(UiMessageToken token, ActionResult& result,
                        Image& image);

  // Requests cancellation without blocking the overlay callback. Call join()
  // after the overlay has returned when the caller needs a completed worker.
  void cancel() noexcept;
  void join() noexcept;

  // Split shutdown for the application coordinator. beginShutdown() only
  // requests cancellation; joinUntil() waits up to the shared deadline;
  // finishShutdown() releases messages after the worker has joined.
  void beginShutdown() noexcept;
  bool joinUntil(std::chrono::steady_clock::time_point deadline) noexcept;
  void finishShutdown() noexcept;

  // Rejects new work, requests cancellation and joins the worker. Idempotent.
  void shutdown() noexcept;

  // Releases completion events that no longer have a consumer. Call this
  // after the worker has joined and before destroying the controller's UI.
  void drainMessages() noexcept;

  bool active() const noexcept;
  std::string diagnosticSnapshot() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qingying
