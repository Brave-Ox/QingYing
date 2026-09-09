#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/app/interaction_gate.h"
#include "qingying/window/window_resolver.h"

#include <functional>
#include <optional>
#include <thread>

namespace qingying {

class CaptureEngine;
class PinManager;
class ResultStore;

// UI-thread owner for ordinary visible-screen capture. It performs all
// admission checks before replacing a scope's current result and keeps Pin
// exclusion active for exactly one CaptureEngine call.
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

  CaptureService(const CaptureService&) = delete;
  CaptureService& operator=(const CaptureService&) = delete;
  CaptureService(CaptureService&&) = delete;
  CaptureService& operator=(CaptureService&&) = delete;

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
};

}  // namespace qingying
