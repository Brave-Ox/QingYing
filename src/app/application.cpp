#include "qingying/app/application.hpp"

#include "qingying/app/action_handlers.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/overlay/coordinate_transform.hpp"

#include "resource.h"

#include <commdlg.h>
#include <chrono>
#include <iterator>
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

const wchar_t* longShotFailureText(int error_code) {
  if (error_code == ErrorCode::kLongShotUnsupported) {
    return L"当前窗口或框选区域不支持长截图。\n"
           L"请在受支持应用的可滚动内容区域内重新框选。";
  }
  return L"长截图失败，请重新框选后再试。";
}

}  // namespace

Application::Application(HINSTANCE instance)
    : instance_(instance), longshot_(capture_) {}

Application::~Application() {
  stopLongShotWorker();
  hotkey_.unregisterAll(tray_.hwnd());
}

void Application::registerHandlers() {
  registerAppHandlers(dispatcher_, capture_, export_service_, session_,
                      pin_manager_);
  pin_manager_.setActionCallbacks(
      [this](const Image& image) { return export_service_.copyToClipboard(image); },
      [this](const Image& image) { return saveImage(image); });
}

void Application::installMessageRouter() {
  tray_.setMessageFilter([this](UINT msg, WPARAM wparam, LPARAM lparam,
                                LRESULT* result) -> bool {
    if (msg == WM_HOTKEY && wparam == HotkeyIds::kCapture) {
      onCaptureHotkey();
      *result = 0;
      return true;
    }
    if (msg == WM_QINGYING_BEGIN_CAPTURE) {
      beginCaptureFlow();
      *result = 0;
      return true;
    }
    if (msg == WM_DESTROY) {
      // The tray window owns the process lifetime. Close any nested capture
      // overlay before TrayController posts WM_QUIT, otherwise the overlay's
      // modal loop can outlive the tray window.
      longshot_stop_.store(true);
      longshot_paused_.store(false);
      overlay_.hide();
      return false;
    }
    if (msg == WM_QINGYING_LONGSHOT_COMPLETE) {
      std::unique_ptr<LongShotCompletion> completion(
          reinterpret_cast<LongShotCompletion*>(lparam));
      finishLongShotOnUiThread();
      if (completion == nullptr) {
        longshot_result_ready_ = false;
        pending_overlay_error_ = L"长截图失败，请重新框选后再试。";
        if (!overlay_.postLongShotFinished(false)) {
          overlay_.hide();
        }
        *result = 0;
        return true;
      }
      bool overlay_success = completion->result.ok;
      if (completion->result.ok) {
        session_.setResult(std::move(completion->image));
        longshot_result_ready_ = true;

        // Keep the current behavior: the first completed result is available
        // immediately, while the overlay remains open for further actions.
        ActionRequest copy_request;
        copy_request.type = ActionType::Copy;
        const ActionResult copy_result = dispatcher_.dispatch(copy_request);
        if (!copy_result.ok) {
          overlay_success = false;
          longshot_result_ready_ = false;
          pending_overlay_error_ = L"长截图已生成，但复制到剪贴板失败。";
        }
      } else {
        longshot_result_ready_ = false;
        pending_overlay_error_ =
            longShotFailureText(completion->result.error_code);
      }
      if (!overlay_.postLongShotFinished(overlay_success)) {
        if (overlay_success) {
          longshot_result_ready_ = false;
        }
        overlay_.hide();
      }
      *result = 0;
      return true;
    }
    return false;
  });
}

void Application::onCaptureHotkey() {
  // Defer to message loop — hotkey handler must stay fast (≤300 ms path).
  PostMessageW(tray_.hwnd(), WM_QINGYING_BEGIN_CAPTURE, 0, 0);
}

void Application::saveLastCapture() {
  if (!session_.hasResult()) {
    MessageBoxW(tray_.hwnd(), L"There is no capture to save yet.", L"QingYing",
                MB_OK | MB_ICONINFORMATION);
    return;
  }

  const ActionResult result = saveImage(session_.result());
  if (!result.ok) {
    MessageBoxW(tray_.hwnd(), L"Failed to save the latest capture.", L"QingYing",
                MB_OK | MB_ICONERROR);
  }
}

ActionResult Application::saveImage(const Image& image) {
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
  dialog.hwndOwner = tray_.hwnd();
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

  return export_service_.savePng(image, path);
}

void Application::runCapturePipeline(const SelectionResult& region) {
  if (region.cancelled) {
    return;
  }

  if (region.action == SelectionAction::Edit) {
    if (region.annotated_image.empty()) {
      return;
    }
    session_.setResult(region.annotated_image);
    ActionRequest copy_req;
    copy_req.type = ActionType::Copy;
    dispatcher_.dispatch(copy_req);
    return;
  }

  if (region.action == SelectionAction::LongShot) {
    longshot_result_ready_ = false;
    startLongShot(pending_longshot_request_);
    return;
  }

  // Once an interactive long-shot has completed, the selection toolbar acts
  // on its accumulated result instead of recapturing the current viewport.
  if (longshot_result_ready_) {
    if (region.action == SelectionAction::Save) {
      saveLastCapture();
    } else if (region.action == SelectionAction::Pin) {
      ActionRequest pin_request;
      pin_request.type = ActionType::Pin;
      const ActionResult pin_result = dispatcher_.dispatch(pin_request);
      if (!pin_result.ok) {
        MessageBoxW(tray_.hwnd(), L"Failed to pin the latest capture.",
                    L"QingYing", MB_OK | MB_ICONERROR);
      }
    } else {
      ActionRequest copy_request;
      copy_request.type = ActionType::Copy;
      dispatcher_.dispatch(copy_request);
    }
    longshot_result_ready_ = false;
    return;
  }

  ActionRequest capture_req;
  capture_req.type = ActionType::CaptureRegion;
  capture_req.x = region.x;
  capture_req.y = region.y;
  capture_req.width = region.width;
  capture_req.height = region.height;

  ActionResult capture_result;
  {
    // Pin windows are ordinary topmost windows, so GDI desktop capture would
    // otherwise copy their border and image into the new screenshot.
    auto pin_capture_guard = pin_manager_.temporarilyHideForCapture();
    capture_result = dispatcher_.dispatch(capture_req);
  }
  if (!capture_result.ok) {
    return;
  }

  if (region.action == SelectionAction::Save) {
    saveLastCapture();
    return;
  }

  if (region.action == SelectionAction::Pin) {
    ActionRequest pin_request;
    pin_request.type = ActionType::Pin;
    const ActionResult pin_result = dispatcher_.dispatch(pin_request);
    if (!pin_result.ok) {
      MessageBoxW(tray_.hwnd(), L"Failed to pin the latest capture.",
                  L"QingYing", MB_OK | MB_ICONERROR);
    }
    return;
  }

  ActionRequest copy_req;
  copy_req.type = ActionType::Copy;
  dispatcher_.dispatch(copy_req);
}

void Application::startLongShot(const LongShotRequest& request) {
  stopLongShotWorker();
  longshot_stop_.store(false);
  longshot_paused_.store(false);

  longshot_thread_ = std::thread([this, request] {
    Image image;
    const ActionResult result = longshot_.captureSelection(
        request, image,
        [this](const Image& preview) {
          overlay_.postLongShotPreview(preview);
        },
        [this] {
          while (longshot_paused_.load() && !longshot_stop_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
          }
          return !longshot_stop_.load();
        });

    auto* completion = new (std::nothrow) LongShotCompletion;
    if (completion == nullptr) {
      overlay_.postLongShotFinished(false);
      return;
    }
    completion->result = result;
    completion->image = std::move(image);
    if (!PostMessageW(tray_.hwnd(), WM_QINGYING_LONGSHOT_COMPLETE, 0,
                      reinterpret_cast<LPARAM>(completion))) {
      delete completion;
      overlay_.postLongShotFinished(false);
    }
  });
}

void Application::onLongShotControl(LongShotControl control) {
  if (control == LongShotControl::TogglePause) {
    longshot_paused_.store(!longshot_paused_.load());
  } else if (control == LongShotControl::Stop) {
    longshot_stop_.store(true);
    longshot_paused_.store(false);
  }
}

void Application::finishLongShotOnUiThread() {
  if (longshot_thread_.joinable()) {
    longshot_thread_.join();
  }
}

void Application::stopLongShotWorker() {
  longshot_stop_.store(true);
  longshot_paused_.store(false);
  if (longshot_thread_.joinable()) {
    longshot_thread_.join();
  }
}

void Application::beginCaptureFlow() {
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
  recorded_owner_window_ = reinterpret_cast<std::uintptr_t>(target);
  pending_longshot_request_ = LongShotRequest{};

  // 先截取虚拟桌面作为遮罩界面背景（排除 Pin 窗口）。遮罩基于截图渲染，
  // 其它窗口（含从属浮层）在截图里保持可见，不再被实时 topmost 窗口盖住。
  // 截屏失败时 background 为空，遮罩退回纯半透明遮罩。
  Image background;
  {
    auto pin_capture_guard = pin_manager_.temporarilyHideForCapture();
    const coord::VirtualScreenRect screen = coord::getVirtualScreen();
    capture_.captureRegion(screen.left, screen.top, screen.width, screen.height,
                           background);
  }

  const bool shown = overlay_.show(
      background, [this](const SelectionResult& region) {
        pending_longshot_request_ =
            makeLongShotRequest(recorded_owner_window_, region);
        runCapturePipeline(region);
      },
      [this](LongShotControl control) { onLongShotControl(control); });

  recorded_owner_window_ = 0;
  pending_longshot_request_ = LongShotRequest{};
  if (!shown) {
    pending_overlay_error_.clear();
    return;
  }

  // Never open a modal dialog while the topmost fullscreen overlay exists.
  // Long-shot failure first closes the overlay; only then is the error shown.
  std::wstring error = std::move(pending_overlay_error_);
  pending_overlay_error_.clear();
  if (!error.empty() && IsWindow(tray_.hwnd())) {
    MessageBoxW(tray_.hwnd(), error.c_str(), L"轻映 QingYing",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
  }
}

int Application::run() {
  if (!single_instance_.acquired()) {
    MessageBoxW(nullptr,
                L"QingYing is already running in the system tray.",
                L"QingYing", MB_OK | MB_ICONINFORMATION);
    return 1;
  }

  registerHandlers();

  if (!tray_.create(instance_)) {
    MessageBoxW(nullptr, L"Failed to create system tray icon.", L"QingYing",
                MB_OK | MB_ICONERROR);
    return 2;
  }

  installMessageRouter();

  if (!hotkey_.registerCaptureHotkey(tray_.hwnd())) {
    MessageBoxW(
        tray_.hwnd(),
        L"Failed to register capture hotkey (Ctrl+Shift+Q).\n"
        L"It may be used by another application.",
        L"QingYing", MB_OK | MB_ICONWARNING);
  }

  MSG msg = {};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  hotkey_.unregisterAll(tray_.hwnd());
  return static_cast<int>(msg.wParam);
}

}  // namespace qingying
