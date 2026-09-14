#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/overlay/selection_overlay.hpp"

#include <memory>
#include <chrono>
#include <string>

namespace qingying {

class CaptureEngine;
class CaptureService;
class LongShotController;
class PinManager;
class ResultActionService;
class ResultStore;
class InteractionGate;

// Pure routing decision for one completed selection. Keeping this separate
// from the Win32 views makes the workflow's branch priority testable.
enum class CaptureWorkflowRoute {
  None,
  Edit,
  AnnotatedResult,
  StartLongShot,
  ExistingLongShotResult,
  CaptureRegion,
};

CaptureWorkflowRoute decideCaptureWorkflowRoute(
    const SelectionIntent& selection,
    bool longshot_result_ready,
    bool annotated_result_ready = false) noexcept;

// Owns one interactive capture workflow. Overlay windows are non-modal; this
// object keeps the small continuation state machine that advances selection,
// annotation, and result actions from the application's message loop.
class CaptureWorkflow {
 public:
  CaptureWorkflow(CaptureEngine& capture, CaptureService& capture_service,
                  LongShotController& longshot_controller,
                  ResultStore& results, ResultActionService& result_actions,
                  PinManager& pin_manager,
                  SelectionOverlay& selection_overlay,
                  InteractionGate* gate = nullptr);
  ~CaptureWorkflow();

  CaptureWorkflow(const CaptureWorkflow&) = delete;
  CaptureWorkflow& operator=(const CaptureWorkflow&) = delete;

  // The tray window remains owned by Application. CaptureWorkflow only uses
  // this non-owning handle for message delivery and dialog ownership.
  void setOwnerWindow(HWND owner_window) noexcept;

  // Starts the selection workflow. Returns false only when it cannot start or
  // the SelectionOverlay cannot be shown; completion is asynchronous.
  bool beginSelection();

  // Advances a pending selection/annotation continuation posted by the overlay
  // callbacks. Application forwards WM_QINGYING_WORKFLOW_CONTINUE here.
  void continueWorkflow();

  // Consumes the private completion token posted by the long-shot worker.
  // Application forwards WM_QINGYING_LONGSHOT_COMPLETE here unchanged.
  void handleLongShotCompletion(UiMessageToken token);

  // Both close the interaction and join its worker before releasing occupancy.
  // shutdown() additionally permanently rejects new workflows.
  void cancel();
  // Split shutdown for the application coordinator. The worker-owning
  // services remain alive when joinUntil() reaches the shared deadline.
  void beginShutdown() noexcept;
  bool joinLongShotUntil(
      std::chrono::steady_clock::time_point deadline) noexcept;
  bool joinUiaUntil(std::chrono::steady_clock::time_point deadline) noexcept;
  bool joinUntil(std::chrono::steady_clock::time_point deadline) noexcept;
  std::string longShotDiagnosticSnapshot() const;
  std::string uiaDiagnosticSnapshot() const;
  void finishShutdown() noexcept;
  void shutdown();

  bool active() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qingying
