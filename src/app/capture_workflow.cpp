#include "qingying/app/capture_workflow.hpp"

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/annotate/annotation_overlay.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/app/capture_preview.hpp"
#include "qingying/app/capture_session.hpp"
#include "qingying/app/longshot_controller.hpp"
#include "qingying/app/longshot_request_adapter.hpp"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/overlay/coordinate_transform.hpp"
#include "qingying/pin/pin_manager.hpp"
#include "qingying/window/window_detector.hpp"

#include <Windows.h>
#include <commdlg.h>

#include <atomic>
#include <iterator>
#include <string>
#include <utility>

namespace qingying {

namespace {

const wchar_t* longShotFailureText(int error_code) {
  if (error_code == ErrorCode::kLongShotUnsupported) {
    return L"当前窗口或框选区域不支持长截图。\n"
           L"请在受支持应用的可滚动内容区域内重新框选。";
  }
  return L"长截图失败，请重新框选后再试。";
}

}  // 匿名命名空间

CaptureWorkflowRoute decideCaptureWorkflowRoute(
    const SelectionResult& selection,
    bool longshot_result_ready) noexcept {
  if (selection.cancelled) {
    return CaptureWorkflowRoute::None;
  }
  if (selection.action == SelectionAction::Edit) {
    return CaptureWorkflowRoute::Edit;
  }
  if (!selection.annotated_image.empty()) {
    return CaptureWorkflowRoute::AnnotatedResult;
  }
  if (selection.action == SelectionAction::LongShot) {
    return CaptureWorkflowRoute::StartLongShot;
  }
  if (longshot_result_ready) {
    return CaptureWorkflowRoute::ExistingLongShotResult;
  }
  if (selection.action == SelectionAction::Copy ||
      selection.action == SelectionAction::Save ||
      selection.action == SelectionAction::Pin) {
    return CaptureWorkflowRoute::CaptureRegion;
  }
  return CaptureWorkflowRoute::None;
}

struct CaptureWorkflow::Impl {
  enum class WorkflowStage { Idle, Selecting, Annotating };

  Impl(ActionDispatcher& dispatcher_in, CaptureEngine& capture_in,
       LongShotController& longshot_controller_in,
       ExportService& export_service_in,
       CaptureSession& session_in, PinManager& pin_manager_in,
       SelectionOverlay& selection_overlay_in)
      : dispatcher(dispatcher_in),
        capture(capture_in),
        longshot_controller(longshot_controller_in),
        export_service(export_service_in),
        session(session_in),
        pin_manager(pin_manager_in),
        selection_overlay(selection_overlay_in) {}

  void setOwnerWindow(std::uintptr_t window) noexcept {
    owner_window = reinterpret_cast<HWND>(window);
    longshot_controller.setOwnerWindow(window);
  }

  void postContinuation() {
    if (owner_window != nullptr && IsWindow(owner_window)) {
      PostMessageW(owner_window, WM_QINGYING_WORKFLOW_CONTINUE, 0, 0);
    }
  }

  void showSelectionOverlay() {
    selection_closed = false;
    selection_result_ready = false;
    const bool shown = selection_overlay.show(
        selection_background,
        [this](const SelectionResult& region) {
          const std::uintptr_t target_window =
              region.action == SelectionAction::LongShot
                  ? ownerWindowAtSelection(region)
                  : recorded_owner_window;
          pending_longshot_request =
              makeLongShotRequest(target_window, region);
          if (region.action == SelectionAction::LongShot) {
            runCapturePipeline(region);
            return;
          }
          pending_selection = region;
          selection_result_ready = true;
        },
        [this](LongShotControl control) { onLongShotControl(control); },
        initial_selection,
        [this]() {
          selection_closed = true;
          postContinuation();
        });
    if (!shown) {
      stage = WorkflowStage::Idle;
      active = false;
    }
  }

  std::uintptr_t ownerWindowAtSelection(
      const SelectionResult& region) const noexcept {
    if (region.width <= 0 || region.height <= 0) {
      return recorded_owner_window;
    }

    const int center_x = region.x + region.width / 2;
    const int center_y = region.y + region.height / 2;
    HWND detected_window = nullptr;
    WindowRect detected_rect;
    if (window_detector.detectAt(center_x, center_y, detected_window,
                                 detected_rect) &&
        detected_window != nullptr) {
      return reinterpret_cast<std::uintptr_t>(detected_window);
    }
    return recorded_owner_window;
  }

  Image captureDesktopBackground() {
    Image background;
    auto pin_capture_guard = pin_manager.temporarilyHideForCapture();
    const coord::VirtualScreenRect screen = coord::getVirtualScreen();
    (void)capture.captureRegion(screen.left, screen.top, screen.width,
                                screen.height, background);
    return background;
  }

  bool beginAnnotation(SelectionResult region) {
    // 编辑意图已经离开长截图结果工具栏；无论编辑确认、取消或启动失败，
    // 下一轮普通截图都不得继续消费上一轮长截图的临时结果状态。
    const bool edit_longshot_result = longshot_result_ready && session.hasResult();
    longshot_result_ready = false;
    if (region.cancelled || region.width <= 0 || region.height <= 0) {
      return false;
    }

    Image source = region.annotated_image;
    // 长截图完成后，操作条上的“编辑”必须使用 Session 中的完整长图。
    // 普通长截图没有 annotated_image，而浏览器长截图的预览图也可能在
    // Overlay 关闭时被释放，因此 Session 才是唯一可靠的结果源。
    if (source.empty() && edit_longshot_result) {
      source = session.result();
    }
    if (source.empty()) {
      auto pin_capture_guard = pin_manager.temporarilyHideForCapture();
      const ActionResult captured = capture.captureRegion(
          region.x, region.y, region.width, region.height, source);
      if (!captured.ok || source.empty()) {
        return false;
      }
    }

    pending_selection = std::move(region);
    annotation_result_ready = false;
    stage = WorkflowStage::Annotating;
    // 长图尺寸通常大于屏幕。不要把它钉在结果小预览框的位置，否则编辑器
    // 的画布和工具栏可能完全落在可视区域外；从虚拟桌面左上角打开即可立即
    // 看见可编辑的首屏内容与工具栏。
    const bool source_is_longshot =
        source.width != pending_selection.width ||
        source.height != pending_selection.height;
    const int image_x = source_is_longshot
                            ? GetSystemMetrics(SM_XVIRTUALSCREEN) + 12
                            : pending_selection.x;
    const int image_y = source_is_longshot
                            ? GetSystemMetrics(SM_YVIRTUALSCREEN) + 72
                            : pending_selection.y;
    const bool shown = annotation_overlay.showInPlace(
        owner_window, source, image_x, image_y,
        [this](const AnnotationFinishResult& result) {
          pending_annotation = result;
          annotation_result_ready = true;
          postContinuation();
        });
    if (!shown) {
      stage = WorkflowStage::Idle;
      return false;
    }
    return true;
  }

  ActionResult saveImage(const Image& image) {
    ActionResult result;
    if (image.empty()) {
      result.ok = false;
      result.error_code = ErrorCode::kNotReady;
      result.message = "no image to save";
      return result;
    }

    wchar_t path[MAX_PATH] = L"qingying.png";
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner_window;
    dialog.lpstrFilter =
        L"PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.lpstrDefExt = L"png";
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (!GetSaveFileNameW(&dialog)) {
      result.ok = true;
      result.error_code = ErrorCode::kOk;
      result.message = "save cancelled";
      return result;
    }

    return export_service.savePng(image, path);
  }

  void saveLastCapture() {
    if (!session.hasResult()) {
      MessageBoxW(owner_window, L"There is no capture to save yet.",
                  L"QingYing", MB_OK | MB_ICONINFORMATION);
      return;
    }

    const ActionResult result = saveImage(session.result());
    if (!result.ok) {
      MessageBoxW(owner_window, L"Failed to save the latest capture.",
                  L"QingYing", MB_OK | MB_ICONERROR);
    }
  }

  void dispatchResultAction(SelectionAction action) {
    if (action == SelectionAction::Save) {
      saveLastCapture();
      return;
    }
    if (action == SelectionAction::Pin) {
      ActionRequest pin_request;
      pin_request.type = ActionType::Pin;
      const ActionResult pin_result = dispatcher.dispatch(pin_request);
      if (!pin_result.ok) {
        MessageBoxW(owner_window, L"Failed to pin the latest capture.",
                    L"QingYing", MB_OK | MB_ICONERROR);
      }
      return;
    }
    if (action == SelectionAction::Copy) {
      ActionRequest copy_request;
      copy_request.type = ActionType::Copy;
      (void)dispatcher.dispatch(copy_request);
    }
  }

  void runCapturePipeline(const SelectionResult& region) {
    const CaptureWorkflowRoute route =
        decideCaptureWorkflowRoute(region, longshot_result_ready);
    switch (route) {
      case CaptureWorkflowRoute::AnnotatedResult:
        session.setResult(region.annotated_image);
        dispatchResultAction(region.action);
        return;
      case CaptureWorkflowRoute::StartLongShot:
        longshot_result_ready = false;
        if (!longshot_controller.start(pending_longshot_request)) {
          pending_overlay_error = L"长截图无法启动，请重新框选后再试。";
          if (!selection_overlay.postLongShotFinished(false)) {
            selection_overlay.hide();
          }
        }
        return;
      case CaptureWorkflowRoute::ExistingLongShotResult:
        dispatchResultAction(region.action);
        longshot_result_ready = false;
        return;
      case CaptureWorkflowRoute::CaptureRegion:
        break;
      case CaptureWorkflowRoute::None:
      case CaptureWorkflowRoute::Edit:
        return;
    }

    ActionRequest capture_request;
    capture_request.type = ActionType::CaptureRegion;
    capture_request.x = region.x;
    capture_request.y = region.y;
    capture_request.width = region.width;
    capture_request.height = region.height;

    ActionResult capture_result;
    {
      // Pin 窗口属于普通置顶窗口；否则 GDI 桌面捕获会把它的边框和图像
      // 一并复制到新截图中。
      auto pin_capture_guard = pin_manager.temporarilyHideForCapture();
      capture_result = dispatcher.dispatch(capture_request);
    }
    if (capture_result.ok) {
      dispatchResultAction(region.action);
    }
  }

  void onLongShotControl(LongShotControl control) {
    longshot_controller.handleControl(control);
  }

  void stopLongShotWorker() {
    longshot_controller.cancel();
    longshot_controller.join();
  }

  void handleLongShotCompletion(std::intptr_t payload) {
    ActionResult completion_result;
    Image completion_image;
    const bool completion_received = longshot_controller.handleCompletion(
        payload, completion_result, completion_image);
    if (shutting_down.load() || !active || stage != WorkflowStage::Selecting) {
      return;
    }
    if (!completion_received) {
      longshot_result_ready = false;
      pending_overlay_error = L"长截图失败，请重新框选后再试。";
      if (!selection_overlay.postLongShotFinished(false)) {
        selection_overlay.hide();
        selection_closed = true;
        postContinuation();
      }
      return;
    }

    bool overlay_success = completion_result.ok;
    if (completion_result.ok) {
      session.setResult(std::move(completion_image));
      longshot_result_ready = true;

      // 保持现有行为：第一份完成的结果立即可用，同时保留遮罩以便继续执行操作。
      ActionRequest copy_request;
      copy_request.type = ActionType::Copy;
      const ActionResult copy_result = dispatcher.dispatch(copy_request);
      if (!copy_result.ok) {
        overlay_success = false;
        longshot_result_ready = false;
        pending_overlay_error = L"长截图已生成，但复制到剪贴板失败。";
      }
    } else {
      longshot_result_ready = false;
      pending_overlay_error =
          longShotFailureText(completion_result.error_code);
    }

    if (!selection_overlay.postLongShotFinished(overlay_success)) {
      if (overlay_success) {
        longshot_result_ready = false;
      }
      selection_overlay.hide();
      selection_closed = true;
      postContinuation();
    }
  }

  bool beginSelection() {
    if (active || shutting_down.load() || owner_window == nullptr ||
        !IsWindow(owner_window)) {
      return false;
    }
    active = true;

    // 在捕获或遮罩操作可能改变前台窗口之前，先记录原始顶层目标。
    // LongShotEngine 后续使用这个句柄，不能再从前台窗口推断目标。
    HWND target = GetForegroundWindow();
    if (target != nullptr) {
      const HWND root = GetAncestor(target, GA_ROOT);
      if (root != nullptr) {
        target = root;
      }
    }
    recorded_owner_window = reinterpret_cast<std::uintptr_t>(target);
    pending_longshot_request = LongShotRequest{};
    // 截屏失败时 background 为空，遮罩仍会退回纯半透明模式。
    selection_background = captureDesktopBackground();
    initial_selection = SelectionResult{};
    pending_selection = SelectionResult{};
    pending_annotation = AnnotationFinishResult{};
    longshot_result_ready = false;
    stage = WorkflowStage::Selecting;
    showSelectionOverlay();
    if (!active) {
      finishWorkflow();
      return false;
    }
    return true;
  }

  void continueWorkflow() {
    if (shutting_down.load() || !active) {
      return;
    }

    if (stage == WorkflowStage::Selecting) {
      if (!selection_closed) {
        return;
      }
      if (!selection_result_ready || pending_selection.cancelled) {
        finishWorkflow();
        return;
      }

      const CaptureWorkflowRoute route =
          decideCaptureWorkflowRoute(pending_selection, longshot_result_ready);
      if (route == CaptureWorkflowRoute::Edit) {
        if (!beginAnnotation(pending_selection)) {
          finishWorkflow();
        }
        return;
      }

      runCapturePipeline(pending_selection);
      finishWorkflow();
      return;
    }

    if (stage != WorkflowStage::Annotating || !annotation_result_ready) {
      return;
    }
    if (pending_annotation.cancelled ||
        pending_annotation.rendered_image.empty()) {
      finishWorkflow();
      return;
    }

    pending_selection.annotated_image =
        std::move(pending_annotation.rendered_image);
    session.setResult(pending_selection.annotated_image);

    // 保持原有体验：完成标注立即复制；结果工具栏随后仍可继续保存或钉图。
    ActionRequest copy_request;
    copy_request.type = ActionType::Copy;
    (void)dispatcher.dispatch(copy_request);

    // 编辑器关闭后重新捕获桌面，并把合成图贴回原选区，恢复统一的
    // 复制 / 保存 / Pin 结果操作条。Overlay 不再负责抓图或创建编辑器。
    selection_background = captureDesktopBackground();
    (void)composeCapturePreview(selection_background,
                                pending_selection.annotated_image,
                                pending_selection, coord::getVirtualScreen());
    pending_selection.action = SelectionAction::None;
    pending_selection.cancelled = false;
    initial_selection = std::move(pending_selection);
    pending_selection = SelectionResult{};
    pending_annotation = AnnotationFinishResult{};
    annotation_result_ready = false;
    stage = WorkflowStage::Selecting;
    showSelectionOverlay();
  }

  void finishWorkflow() {
    stopLongShotWorker();
    longshot_result_ready = false;
    recorded_owner_window = 0;
    pending_longshot_request = LongShotRequest{};
    selection_background = Image{};
    initial_selection = SelectionResult{};
    pending_selection = SelectionResult{};
    pending_annotation = AnnotationFinishResult{};
    selection_closed = false;
    selection_result_ready = false;
    annotation_result_ready = false;
    stage = WorkflowStage::Idle;
    active = false;

    if (shutting_down.load()) {
      pending_overlay_error.clear();
      return;
    }

    // 顶层全屏遮罩存在时，不能打开模态对话框。
    // 长截图失败时先关闭遮罩，再显示错误信息。
    std::wstring error = std::move(pending_overlay_error);
    pending_overlay_error.clear();
    if (!error.empty() && IsWindow(owner_window)) {
      MessageBoxW(owner_window, error.c_str(), L"轻映 QingYing",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    }
  }

  void cancel() {
    longshot_controller.cancel();
    annotation_overlay.closeSilently();
    selection_overlay.hide();
    active = false;
    stage = WorkflowStage::Idle;
    longshot_result_ready = false;
    recorded_owner_window = 0;
    pending_longshot_request = LongShotRequest{};
    selection_background = Image{};
    initial_selection = SelectionResult{};
    pending_selection = SelectionResult{};
    pending_annotation = AnnotationFinishResult{};
    selection_closed = false;
    selection_result_ready = false;
    annotation_result_ready = false;
    pending_overlay_error.clear();
  }

  void shutdown() {
    if (shutting_down.exchange(true)) {
      return;
    }
    cancel();
    longshot_controller.shutdown();
    active = false;
    stage = WorkflowStage::Idle;
    longshot_result_ready = false;
    recorded_owner_window = 0;
    pending_longshot_request = LongShotRequest{};
    selection_background = Image{};
    initial_selection = SelectionResult{};
    pending_selection = SelectionResult{};
    pending_annotation = AnnotationFinishResult{};
    selection_closed = false;
    selection_result_ready = false;
    annotation_result_ready = false;
    pending_overlay_error.clear();
    owner_window = nullptr;
  }

  ActionDispatcher& dispatcher;
  CaptureEngine& capture;
  LongShotController& longshot_controller;
  ExportService& export_service;
  CaptureSession& session;
  PinManager& pin_manager;
  SelectionOverlay& selection_overlay;
  AnnotationOverlay annotation_overlay;
  WindowDetector window_detector;
  HWND owner_window{nullptr};
  WorkflowStage stage{WorkflowStage::Idle};
  std::uintptr_t recorded_owner_window{0};
  LongShotRequest pending_longshot_request{};
  Image selection_background;
  SelectionResult initial_selection;
  SelectionResult pending_selection;
  AnnotationFinishResult pending_annotation;
  std::atomic<bool> shutting_down{false};
  bool active{false};
  bool longshot_result_ready{false};
  bool selection_closed{false};
  bool selection_result_ready{false};
  bool annotation_result_ready{false};
  std::wstring pending_overlay_error;
};

CaptureWorkflow::CaptureWorkflow(
    ActionDispatcher& dispatcher, CaptureEngine& capture,
    LongShotController& longshot_controller, ExportService& export_service,
    CaptureSession& session, PinManager& pin_manager,
    SelectionOverlay& selection_overlay)
    : impl_(std::make_unique<Impl>(dispatcher, capture, longshot_controller,
                                   export_service, session, pin_manager,
                                   selection_overlay)) {}

CaptureWorkflow::~CaptureWorkflow() {
  impl_->shutdown();
}

void CaptureWorkflow::setOwnerWindow(std::uintptr_t owner_window) noexcept {
  impl_->setOwnerWindow(owner_window);
}

bool CaptureWorkflow::beginSelection() {
  return impl_->beginSelection();
}

void CaptureWorkflow::continueWorkflow() {
  impl_->continueWorkflow();
}

void CaptureWorkflow::handleLongShotCompletion(std::intptr_t payload) {
  impl_->handleLongShotCompletion(payload);
}

ActionResult CaptureWorkflow::saveImage(const Image& image) {
  return impl_->saveImage(image);
}

void CaptureWorkflow::cancel() {
  impl_->cancel();
}

void CaptureWorkflow::shutdown() {
  impl_->shutdown();
}

bool CaptureWorkflow::active() const noexcept {
  return impl_->active;
}

}  // qingying 命名空间
