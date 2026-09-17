#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/i_action_handler.hpp"
#include "qingying/action/types.hpp"
#include "qingying/app/interaction_gate.h"
#include "qingying/window/window_resolver.h"

#include <functional>
#include <optional>
#include <thread>
#include <memory>
#include <chrono>

namespace qingying {

class CaptureEngine;
class PinManager;
class ResultStore;

// UI owner for capture admission/publication. Its dedicated bounded executor
// performs acquisition and image transforms; UI retains all window guards.
class CaptureService final {
 public:
  using CaptureInvoker =
      std::function<ActionResult(const ScreenPhysicalRect&, Image&)>;
  using PrimaryMonitorProvider = std::function<ScreenPhysicalRect()>;

  CaptureService(CaptureEngine& capture, ResultStore& results,
                 PinManager& pins, InteractionGate& gate,
                 CaptureInvoker capture_invoker = {},
                 PrimaryMonitorProvider primary_monitor = {},
                 WindowResolver::Catalog window_catalog = {});
  ~CaptureService();

  using ImageTransform = std::function<void(Image&)>;
  using ImageCompletion = std::function<void(ActionResult, Image)>;
  // All callbacks and window/ResultStore mutations run on the owning UI thread.
  void captureImageAsync(const ActionRequest& request,
                        const ScreenPhysicalRect& region,
                        ImageCompletion completion,
                        const InteractionGate::Guard* owner = nullptr,
                        ImageTransform transform = {});
  void captureAsync(const ActionRequest& request,
                    const ScreenPhysicalRect& region,
                    ActionCompletion completion,
                    const InteractionGate::Guard* owner = nullptr);
  void captureActionAsync(const ActionRequest& request, ActionCompletion completion);
  void beginStop() noexcept;
  bool joinUntil(std::chrono::steady_clock::time_point deadline) noexcept;
  std::string diagnosticSnapshot() const;

  CaptureService(const CaptureService&) = delete;
  CaptureService& operator=(const CaptureService&) = delete;
  CaptureService(CaptureService&&) = delete;
  CaptureService& operator=(CaptureService&&) = delete;

  // Synchronous compatibility entry points; production uses the async APIs.
  ActionResult capture(const ActionRequest& request,
                       const ScreenPhysicalRect& region,
                       const InteractionGate::Guard* interaction_owner = nullptr);
  ActionResult cropCenter(
      const ActionRequest& request, int width, int height,
      const InteractionGate::Guard* interaction_owner = nullptr);
  ActionResult captureWindow(
      const ActionRequest& request, const CaptureWindowRequest& window,
      const InteractionGate::Guard* interaction_owner = nullptr);

  static std::optional<ScreenPhysicalRect> centeredRect(
      const ScreenPhysicalRect& monitor, int width, int height) noexcept;

 private:
  void checkThread() const;
  static bool validRegion(const ScreenPhysicalRect& region) noexcept;

  CaptureEngine& capture_;
  ResultStore& results_;
  PinManager& pins_;
  InteractionGate& gate_;
  CaptureInvoker capture_invoker_;
  PrimaryMonitorProvider primary_monitor_;
  WindowResolver window_resolver_;
  std::thread::id ui_thread_{std::this_thread::get_id()};
  struct AsyncImpl;
  std::unique_ptr<AsyncImpl> async_;
};

}  // namespace qingying
