#include "qingying/app/capture_workflow.hpp"

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/annotate/annotation_overlay.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/app/capture_preview.hpp"
#include "qingying/app/capture_session.hpp"
#include "qingying/app/longshot_request_adapter.hpp"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/overlay/coordinate_transform.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <Windows.h>
#include <commdlg.h>

#include <atomic>
#include <chrono>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <utility>

namespace qingying {

namespace {

struct LongShotCompletion {
  ActionResult result;
  Image image;
};

const wchar_t* longShotFailureText(int error_code) {
  if (error_code == ErrorCode::kLongShotUnsupported) {
    return L"当前窗口或框选区域不支持长截图。\n"
           L"请在受支持应用的可滚动内容区域内重新框选。";
  }
  return L"长截图失败，请重新框选后再试。";
}

}  // namespace

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
  Impl(ActionDispatcher& dispatcher_in, CaptureEngine& capture_in,
       LongShotEngine& longshot_in, ExportService& export_service_in,
       CaptureSession& session_in, PinManager& pin_manager_in,
       SelectionOverlay& selection_overlay_in)
      : dispatcher(dispatcher_in),
        capture(capture_in),
        longshot(longshot_in),
        export_service(export_service_in),
        session(session_in),
        pin_manager(pin_manager_in),
        selection_overlay(selection_overlay_in) {}

  void setOwnerWindow(std::uintptr_t window) noexcept {
    owner_window = reinterpret_cast<HWND>(window);
  }

  Image captureDesktopBackground() {
    Image background;
    auto pin_capture_guard = pin_manager.temporarilyHideForCapture();
    const coord::VirtualScreenRect screen = coord::getVirtualScreen();
    (void)capture.captureRegion(screen.left, screen.top, screen.width,
                                screen.height, background);
    return background;
  }

  bool editSelection(SelectionResult& region) {
    // 编辑意图已经离开长截图结果工具栏；无论编辑确认、取消或启动失败，
    // 下一轮普通截图都不得继续消费上一轮长截图的临时结果状态。
    longshot_result_ready = false;
    if (region.cancelled || region.width <= 0 || region.height <= 0) {
      return false;
    }

    Image source = region.annotated_image;
    if (source.empty()) {
      auto pin_capture_guard = pin_manager.temporarilyHideForCapture();
      const ActionResult captured = capture.captureRegion(
          region.x, region.y, region.width, region.height, source);
      if (!captured.ok || source.empty()) {
        return false;
      }
    }

    AnnotationFinishResult finish;
    const bool shown = annotation_overlay.showInPlace(
        owner_window, source, region.x, region.y,
        [&finish](const AnnotationFinishResult& result) { finish = result; });
    if (!shown || finish.cancelled || finish.rendered_image.empty()) {
      return false;
    }

    region.annotated_image = std::move(finish.rendered_image);
    session.setResult(region.annotated_image);

    // 保持原有体验：完成标注立即复制；结果工具栏随后仍可继续保存或钉图。
    ActionRequest copy_request;
    copy_request.type = ActionType::Copy;
    (void)dispatcher.dispatch(copy_request);
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
        startLongShot(pending_longshot_request);
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
      // Pin windows are ordinary topmost windows, so GDI desktop capture would
      // otherwise copy their border and image into the new screenshot.
      auto pin_capture_guard = pin_manager.temporarilyHideForCapture();
      capture_result = dispatcher.dispatch(capture_request);
    }
    if (capture_result.ok) {
      dispatchResultAction(region.action);
    }
  }

  void startLongShot(const LongShotRequest& request) {
    stopLongShotWorker();
    longshot_stop.store(false);
    longshot_paused.store(false);
    const HWND completion_window = owner_window;

    longshot_thread = std::thread([this, request, completion_window] {
      Image image;
      const ActionResult result = longshot.captureSelection(
          request, image,
          [this](const Image& preview) {
            selection_overlay.postLongShotPreview(preview);
          },
          [this] {
            while (longshot_paused.load() && !longshot_stop.load()) {
              std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
            return !longshot_stop.load();
          });

      if (shutting_down.load()) {
        return;
      }

      std::unique_ptr<LongShotCompletion> completion(
          new (std::nothrow) LongShotCompletion);
      if (completion == nullptr) {
        selection_overlay.postLongShotFinished(false);
        return;
      }
      completion->result = result;
      completion->image = std::move(image);
      if (!PostMessageW(completion_window, WM_QINGYING_LONGSHOT_COMPLETE, 0,
                        reinterpret_cast<LPARAM>(completion.get()))) {
        selection_overlay.postLongShotFinished(false);
        return;
      }
      (void)completion.release();
    });
  }

  void onLongShotControl(LongShotControl control) {
    if (control == LongShotControl::TogglePause) {
      longshot_paused.store(!longshot_paused.load());
    } else if (control == LongShotControl::Stop) {
      longshot_stop.store(true);
      longshot_paused.store(false);
    }
  }

  void finishLongShotOnUiThread() {
    if (longshot_thread.joinable()) {
      longshot_thread.join();
    }
  }

  void stopLongShotWorker() {
    longshot_stop.store(true);
    longshot_paused.store(false);
    if (longshot_thread.joinable()) {
      longshot_thread.join();
    }
  }

  void handleLongShotCompletion(std::intptr_t payload) {
    std::unique_ptr<LongShotCompletion> completion(
        reinterpret_cast<LongShotCompletion*>(payload));
    finishLongShotOnUiThread();
    if (shutting_down.load()) {
      return;
    }
    if (completion == nullptr) {
      longshot_result_ready = false;
      pending_overlay_error = L"长截图失败，请重新框选后再试。";
      if (!selection_overlay.postLongShotFinished(false)) {
        selection_overlay.hide();
      }
      return;
    }

    bool overlay_success = completion->result.ok;
    if (completion->result.ok) {
      session.setResult(std::move(completion->image));
      longshot_result_ready = true;

      // Keep the current behavior: the first completed result is available
      // immediately, while the overlay remains open for further actions.
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
          longShotFailureText(completion->result.error_code);
    }

    if (!selection_overlay.postLongShotFinished(overlay_success)) {
      if (overlay_success) {
        longshot_result_ready = false;
      }
      selection_overlay.hide();
    }
  }

  bool beginSelection() {
    if (active || shutting_down.load() || owner_window == nullptr ||
        !IsWindow(owner_window)) {
      return false;
    }
    active = true;

    // Capture the original top-level target before any capture/overlay work
    // can change the foreground window. LongShotEngine consumes this recorded
    // handle later; it must not infer the target from the foreground window.
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
    Image background = captureDesktopBackground();
    SelectionResult initial_selection;
    bool overlay_failed = false;

    for (;;) {
      SelectionResult completed;
      bool completed_received = false;
      const bool shown = selection_overlay.show(
          background,
          [this, &completed, &completed_received](
              const SelectionResult& region) {
            pending_longshot_request =
                makeLongShotRequest(recorded_owner_window, region);
            if (region.action == SelectionAction::LongShot) {
              runCapturePipeline(region);
              return;
            }
            completed = region;
            completed_received = true;
          },
          [this](LongShotControl control) { onLongShotControl(control); },
          initial_selection);

      if (!shown) {
        overlay_failed = true;
        break;
      }
      if (shutting_down.load() || !completed_received || completed.cancelled) {
        break;
      }

      if (decideCaptureWorkflowRoute(completed, longshot_result_ready) ==
          CaptureWorkflowRoute::Edit) {
        if (!editSelection(completed)) {
          break;
        }

        // 编辑器关闭后重新抓桌面，并把合成图贴回原选区，恢复统一的
        // Copy / Save / Pin 结果工具栏。Overlay 不再负责抓图或创建编辑器。
        background = captureDesktopBackground();
        (void)composeCapturePreview(background, completed.annotated_image,
                                    completed, coord::getVirtualScreen());
        completed.action = SelectionAction::None;
        completed.cancelled = false;
        initial_selection = std::move(completed);
        continue;
      }

      runCapturePipeline(completed);
      break;
    }

    stopLongShotWorker();
    longshot_result_ready = false;
    recorded_owner_window = 0;
    pending_longshot_request = LongShotRequest{};
    if (overlay_failed || shutting_down.load()) {
      active = false;
      pending_overlay_error.clear();
      return !overlay_failed;
    }

    // Never open a modal dialog while the topmost fullscreen overlay exists.
    // Long-shot failure first closes the overlay; only then is the error shown.
    std::wstring error = std::move(pending_overlay_error);
    pending_overlay_error.clear();
    if (!error.empty() && IsWindow(owner_window)) {
      MessageBoxW(owner_window, error.c_str(), L"轻映 QingYing",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    }
    active = false;
    return true;
  }

  void cancel() {
    longshot_stop.store(true);
    longshot_paused.store(false);
    annotation_overlay.hide();
    selection_overlay.hide();
  }

  void shutdown() {
    if (shutting_down.exchange(true)) {
      return;
    }
    cancel();
    stopLongShotWorker();
    active = false;
    longshot_result_ready = false;
    recorded_owner_window = 0;
    pending_longshot_request = LongShotRequest{};
    pending_overlay_error.clear();
    owner_window = nullptr;
  }

  ActionDispatcher& dispatcher;
  CaptureEngine& capture;
  LongShotEngine& longshot;
  ExportService& export_service;
  CaptureSession& session;
  PinManager& pin_manager;
  SelectionOverlay& selection_overlay;
  AnnotationOverlay annotation_overlay;
  HWND owner_window{nullptr};
  std::uintptr_t recorded_owner_window{0};
  LongShotRequest pending_longshot_request{};
  std::thread longshot_thread;
  std::atomic<bool> longshot_stop{false};
  std::atomic<bool> longshot_paused{false};
  std::atomic<bool> shutting_down{false};
  bool active{false};
  bool longshot_result_ready{false};
  std::wstring pending_overlay_error;
};

CaptureWorkflow::CaptureWorkflow(
    ActionDispatcher& dispatcher, CaptureEngine& capture,
    LongShotEngine& longshot, ExportService& export_service,
    CaptureSession& session, PinManager& pin_manager,
    SelectionOverlay& selection_overlay)
    : impl_(std::make_unique<Impl>(dispatcher, capture, longshot,
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

}  // namespace qingying
