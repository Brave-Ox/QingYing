#pragma once

#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/overlay/selection_overlay.hpp"

#include <cstdint>
#include <memory>

namespace qingying {

class ActionDispatcher;
class CaptureEngine;
class CaptureSession;
class ExportService;
class LongShotEngine;
class PinManager;

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
    const SelectionResult& selection,
    bool longshot_result_ready) noexcept;

// Owns one interactive capture workflow. The current implementation keeps the
// existing synchronous Overlay contract; making both overlays non-modal is a
// later lifecycle-only change and does not move orchestration back to app.
class CaptureWorkflow {
 public:
  CaptureWorkflow(ActionDispatcher& dispatcher, CaptureEngine& capture,
                  LongShotEngine& longshot, ExportService& export_service,
                  CaptureSession& session, PinManager& pin_manager,
                  SelectionOverlay& selection_overlay);
  ~CaptureWorkflow();

  CaptureWorkflow(const CaptureWorkflow&) = delete;
  CaptureWorkflow& operator=(const CaptureWorkflow&) = delete;

  // The tray window remains owned by Application. CaptureWorkflow only uses
  // this non-owning handle for message delivery and dialog ownership.
  void setOwnerWindow(std::uintptr_t owner_window) noexcept;

  // Runs the current synchronous selection workflow. Returns false only when
  // it cannot start or the SelectionOverlay cannot be shown.
  bool beginSelection();

  // Consumes the private completion payload posted by the long-shot worker.
  // Application forwards WM_QINGYING_LONGSHOT_COMPLETE here unchanged.
  void handleLongShotCompletion(std::intptr_t payload);

  // Used by PinManager's per-window save callback.
  ActionResult saveImage(const Image& image);

  // cancel() requests the current interaction to close. shutdown() additionally
  // joins the worker and permanently rejects new workflows.
  void cancel();
  void shutdown();

  bool active() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qingying
