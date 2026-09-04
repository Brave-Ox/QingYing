#include "qingying/app/capture_workflow.hpp"

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/annotate/annotation_overlay.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/app/capture_preview.hpp"
#include "qingying/app/longshot_controller.hpp"
#include "qingying/app/longshot_request_adapter.hpp"
#include "qingying/app/result_action_service.h"
#include "qingying/app/result_store.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/overlay/coordinate_transform.hpp"
#include "qingying/pin/pin_manager.hpp"
#include "qingying/window/window_detector.hpp"

#include <Windows.h>

#include <atomic>
#include <string>
#include <utility>

namespace qingying {

namespace {

const wchar_t* longShotFailureStageText(const std::string& stage) {
  if (stage == "request_validation") {
    return L"请求校验";
  }
  if (stage == "safety_limit") {
    return L"安全限制";
  }
  if (stage == "profile_resolution") {
    return L"滚动目标识别";
  }
  if (stage == "scroll_input") {
    return L"滚动输入";
  }
  if (stage == "scroll_settle") {
    return L"等待滚动稳定";
  }
  if (stage == "initial_capture" || stage == "frame_capture") {
    return L"屏幕采集";
  }
  if (stage == "frame_validation") {
    return L"图像帧校验";
  }
  if (stage == "overlap_detection") {
    return L"重叠区域匹配";
  }
  if (stage == "stitching") {
    return L"图像拼接";
  }
  return L"未知阶段";
}

std::wstring longShotFailureText(const ActionResult& result) {
  if (result.error_code == ErrorCode::kLongShotUnsupported &&
      (result.failure_stage.empty() ||
       (result.failure_stage == "profile_resolution" &&
        result.failure_frame == 0))) {
    return L"当前窗口或框选区域不支持长截图。\n"
           L"请在受支持应用的可滚动内容区域内重新框选。";
  }

  std::wstring text = L"长截图失败";
  if (!result.failure_stage.empty()) {
    text += L"\n失败阶段：";
    text += longShotFailureStageText(result.failure_stage);
  }
  if (result.failure_frame > 0) {
    text += L"（第 ";
    text += std::to_wstring(result.failure_frame);
    text += L" 帧）";
  }
  text += L"。\n请重新框选后再试。";
  return text;
}

}  // 匿名命名空间

CaptureWorkflowRoute decideCaptureWorkflowRoute(
    const SelectionIntent& selection,
    bool longshot_result_ready,
    bool annotated_result_ready) noexcept {
  if (!selection.valid()) {
    return CaptureWorkflowRoute::None;
  }
  if (selection.action == SelectionAction::Edit) {
    return CaptureWorkflowRoute::Edit;
  }
  if (annotated_result_ready) {
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
       ResultStore& results_in, ResultActionService& result_actions_in,
       PinManager& pin_manager_in,
       SelectionOverlay& selection_overlay_in)
      : dispatcher(dispatcher_in),
        capture(capture_in),
        longshot_controller(longshot_controller_in),
        results(results_in),
        result_actions(result_actions_in),
        pin_manager(pin_manager_in),
        selection_overlay(selection_overlay_in) {}

  void setOwnerWindow(HWND window) noexcept {
    owner_window = window;
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
        [this](const SelectionIntent& region) {
          const HWND target_window =
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
        },
        annotated_result_ready);
    if (!shown) {
      stage = WorkflowStage::Idle;
      active = false;
    }
  }

  HWND ownerWindowAtSelection(const SelectionIntent& region) const noexcept {
    if (!region.valid()) {
      return recorded_owner_window;
    }

    const int center_x = region.x + region.width / 2;
    const int center_y = region.y + region.height / 2;
    HWND detected_window = nullptr;
    WindowRect detected_rect;
    if (window_detector.detectAt(center_x, center_y, detected_window,
                                 detected_rect) &&
        detected_window != nullptr) {
      return detected_window;
    }
    return recorded_owner_window;
  }

  Image captureDesktopBackground() {
    Image background;
    auto pin_capture_guard = pin_manager.temporarilyHideForCapture();
    const coord::VirtualScreenRect screen = coord::getVirtualScreen();
    (void)capture.captureRegion(
        ScreenPhysicalRect{screen.x, screen.y, screen.width, screen.height},
        background);
    return background;
  }

  bool beginAnnotation(SelectionIntent region) {
    // 编辑意图已经离开长截图结果工具栏；无论编辑确认、取消或启动失败，
    // 下一轮普通截图都不得继续消费上一轮长截图的临时结果状态。
    longshot_result_ready = false;
    if (!region.valid()) {
      return false;
    }

    Image source;
    if (annotated_result_ready && active_result_id != kInvalidResultId) {
      const Image* existing = results.getImage(active_result_id);
      if (existing != nullptr) {
        source = *existing;
      }
    }
    if (source.empty()) {
      auto pin_capture_guard = pin_manager.temporarilyHideForCapture();
      const ActionResult captured = capture.captureRegion(region.screenRect(),
                                                          source);
      if (!captured.ok || source.empty()) {
        return false;
      }
    }

    annotated_result_ready = false;
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

  void dispatchResultAction(SelectionAction action, ResultId result_id) {
    if (action == SelectionAction::Save) {
      (void)result_actions.save(result_id);
      return;
    }
    if (action == SelectionAction::Pin) {
      const ActionResult pin_result = result_actions.pin(result_id);
      if (!pin_result.ok) {
        MessageBoxW(owner_window, L"Failed to pin the latest capture.",
                    L"QingYing", MB_OK | MB_ICONERROR);
      }
      return;
    }
    if (action == SelectionAction::Copy) {
      (void)result_actions.copy(result_id);
    }
  }

  void runCapturePipeline(const SelectionIntent& region) {
    const CaptureWorkflowRoute route = decideCaptureWorkflowRoute(
        region, longshot_result_ready, annotated_result_ready);
    switch (route) {
      case CaptureWorkflowRoute::AnnotatedResult:
        if (annotated_result_ready &&
            results.getImage(active_result_id) != nullptr) {
          dispatchResultAction(region.action, active_result_id);
        }
        annotated_result_ready = false;
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
        dispatchResultAction(region.action, active_result_id);
        longshot_result_ready = false;
        return;
      case CaptureWorkflowRoute::CaptureRegion:
        break;
      case CaptureWorkflowRoute::None:
      case CaptureWorkflowRoute::Edit:
        return;
    }

    const ScreenPhysicalRect screen_region = region.screenRect();
    ActionRequest capture_request = makeActionRequest(
        CaptureRegionRequest{screen_region});

    ActionResult capture_result;
    {
      // Pin 窗口属于普通置顶窗口；否则 GDI 桌面捕获会把它的边框和图像
      // 一并复制到新截图中。
      auto pin_capture_guard = pin_manager.temporarilyHideForCapture();
      capture_result = dispatcher.dispatch(capture_request);
    }
    if (capture_result.ok) {
      active_result_id = results.currentId();
      if (active_result_id != kInvalidResultId) {
        dispatchResultAction(region.action, active_result_id);
      }
    }
  }

  void onLongShotControl(LongShotControl control) {
    longshot_controller.handleControl(control);
  }

  void stopLongShotWorker() {
    longshot_controller.cancel();
    longshot_controller.join();
    longshot_controller.drainMessages();
  }

  void handleLongShotCompletion(UiMessageToken token) {
    ActionResult completion_result;
    Image completion_image;
    const bool completion_received = longshot_controller.handleCompletion(
        token, completion_result, completion_image);
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
      active_result_id = results.publish(std::move(completion_image));
      longshot_result_ready = active_result_id != kInvalidResultId;
      if (!longshot_result_ready) {
        overlay_success = false;
        pending_overlay_error = L"长截图生成了无效结果，请重新框选后再试。";
      } else {
        // 保持现有行为：第一份完成的结果立即可用，同时保留遮罩以便继续执行操作。
        const ActionResult copy_result = result_actions.copy(active_result_id);
        if (!copy_result.ok) {
          overlay_success = false;
          longshot_result_ready = false;
          pending_overlay_error = L"长截图已生成，但复制到剪贴板失败。";
        }
      }
      if (!overlay_success) {
        active_result_id = kInvalidResultId;
      }
    } else {
      longshot_result_ready = false;
      pending_overlay_error =
          longShotFailureText(completion_result);
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
    active_result_id = kInvalidResultId;

    // 在捕获或遮罩操作可能改变前台窗口之前，先记录原始顶层目标。
    // LongShotEngine 后续使用这个句柄，不能再从前台窗口推断目标。
    HWND target = GetForegroundWindow();
    if (target != nullptr) {
      const HWND root = GetAncestor(target, GA_ROOT);
      if (root != nullptr) {
        target = root;
      }
    }
    recorded_owner_window = target;
    pending_longshot_request = LongShotRequest{};
    // 截屏失败时 background 为空，遮罩仍会退回纯半透明模式。
    selection_background = captureDesktopBackground();
    initial_selection = SelectionIntent{};
    pending_selection = SelectionIntent{};
    pending_annotation = AnnotationFinishResult{};
    longshot_result_ready = false;
    annotated_result_ready = false;
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
      if (!selection_result_ready || !pending_selection.valid()) {
        finishWorkflow();
        return;
      }

      const CaptureWorkflowRoute route =
          decideCaptureWorkflowRoute(pending_selection, longshot_result_ready,
                                     annotated_result_ready);
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

    Image annotated_image = std::move(pending_annotation.rendered_image);
    active_result_id = results.publish(std::move(annotated_image));
    if (active_result_id == kInvalidResultId) {
      finishWorkflow();
      return;
    }
    const Image* annotated_result = results.getImage(active_result_id);
    if (annotated_result == nullptr) {
      finishWorkflow();
      return;
    }

    // 保持原有体验：完成标注立即复制；结果工具栏随后仍可继续保存或钉图。
    (void)result_actions.copy(active_result_id);

    // 编辑器关闭后重新捕获桌面，并把合成图贴回原选区，恢复统一的
    // 复制 / 保存 / Pin 结果操作条。Overlay 不再负责抓图或创建编辑器。
    selection_background = captureDesktopBackground();
    (void)composeCapturePreview(selection_background,
                                *annotated_result, pending_selection.screenRect(),
                                coord::getVirtualScreen());
    pending_selection.action = SelectionAction::None;
    pending_selection.cancelled = false;
    initial_selection = std::move(pending_selection);
    pending_selection = SelectionIntent{};
    pending_annotation = AnnotationFinishResult{};
    annotation_result_ready = false;
    annotated_result_ready = true;
    stage = WorkflowStage::Selecting;
    showSelectionOverlay();
  }

  void finishWorkflow() {
    stopLongShotWorker();
    longshot_result_ready = false;
    active_result_id = kInvalidResultId;
    recorded_owner_window = 0;
    pending_longshot_request = LongShotRequest{};
    selection_background = Image{};
    initial_selection = SelectionIntent{};
    pending_selection = SelectionIntent{};
    pending_annotation = AnnotationFinishResult{};
    selection_closed = false;
    selection_result_ready = false;
    annotation_result_ready = false;
    annotated_result_ready = false;
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
    active_result_id = kInvalidResultId;
    recorded_owner_window = 0;
    pending_longshot_request = LongShotRequest{};
    selection_background = Image{};
    initial_selection = SelectionIntent{};
    pending_selection = SelectionIntent{};
    pending_annotation = AnnotationFinishResult{};
    selection_closed = false;
    selection_result_ready = false;
    annotation_result_ready = false;
    annotated_result_ready = false;
    pending_overlay_error.clear();
  }

  void shutdown() {
    if (shutting_down.exchange(true)) {
      return;
    }
    // Stop and join the worker before draining events or destroying windows.
    longshot_controller.shutdown();
    selection_overlay.drainMessages();
    annotation_overlay.closeSilently();
    selection_overlay.hide();
    active = false;
    stage = WorkflowStage::Idle;
    longshot_result_ready = false;
    active_result_id = kInvalidResultId;
    recorded_owner_window = 0;
    pending_longshot_request = LongShotRequest{};
    selection_background = Image{};
    initial_selection = SelectionIntent{};
    pending_selection = SelectionIntent{};
    pending_annotation = AnnotationFinishResult{};
    selection_closed = false;
    selection_result_ready = false;
    annotation_result_ready = false;
    annotated_result_ready = false;
    pending_overlay_error.clear();
    owner_window = nullptr;
  }

  ActionDispatcher& dispatcher;
  CaptureEngine& capture;
  LongShotController& longshot_controller;
  ResultStore& results;
  ResultActionService& result_actions;
  PinManager& pin_manager;
  SelectionOverlay& selection_overlay;
  AnnotationOverlay annotation_overlay;
  WindowDetector window_detector;
  HWND owner_window{nullptr};
  WorkflowStage stage{WorkflowStage::Idle};
  HWND recorded_owner_window{nullptr};
  LongShotRequest pending_longshot_request{};
  Image selection_background;
  SelectionIntent initial_selection;
  SelectionIntent pending_selection;
  AnnotationFinishResult pending_annotation;
  std::atomic<bool> shutting_down{false};
  bool active{false};
  bool longshot_result_ready{false};
  bool annotated_result_ready{false};
  ResultId active_result_id{kInvalidResultId};
  bool selection_closed{false};
  bool selection_result_ready{false};
  bool annotation_result_ready{false};
  std::wstring pending_overlay_error;
};

CaptureWorkflow::CaptureWorkflow(
    ActionDispatcher& dispatcher, CaptureEngine& capture,
    LongShotController& longshot_controller, ResultStore& results,
    ResultActionService& result_actions, PinManager& pin_manager,
    SelectionOverlay& selection_overlay)
    : impl_(std::make_unique<Impl>(dispatcher, capture, longshot_controller,
                                   results, result_actions, pin_manager,
                                   selection_overlay)) {}

CaptureWorkflow::~CaptureWorkflow() {
  impl_->shutdown();
}

void CaptureWorkflow::setOwnerWindow(HWND owner_window) noexcept {
  impl_->setOwnerWindow(owner_window);
}

bool CaptureWorkflow::beginSelection() {
  return impl_->beginSelection();
}

void CaptureWorkflow::continueWorkflow() {
  impl_->continueWorkflow();
}

void CaptureWorkflow::handleLongShotCompletion(UiMessageToken token) {
  impl_->handleLongShotCompletion(token);
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
