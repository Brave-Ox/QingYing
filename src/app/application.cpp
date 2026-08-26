#include "qingying/app/application.hpp"

#include "qingying/app/action_handlers.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/action/image.hpp"
#include "qingying/action/types.hpp"
#include "qingying/overlay/coordinate_transform.hpp"

#include "resource.h"

#include <commdlg.h>
#include <iterator>
#include <utility>

namespace qingying {

Application::Application(HINSTANCE instance)
    : instance_(instance), longshot_(capture_) {}

Application::~Application() {
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
  tray_.setMessageFilter([this](UINT msg, WPARAM wparam, LPARAM /*lparam*/,
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

  if (region.action == SelectionAction::LongShot) {
    session_.clear();
    Image image;
    ActionResult longshot_result;
    {
      auto pin_capture_guard = pin_manager_.temporarilyHideForCapture();
      longshot_result = longshot_.captureSelection(pending_longshot_request_,
                                                   image);
    }
    if (!longshot_result.ok) {
      MessageBoxW(tray_.hwnd(), L"Failed to capture the long screenshot.",
                  L"QingYing", MB_OK | MB_ICONERROR);
      return;
    }
    session_.setResult(std::move(image));

    // LongShot produces the same CaptureSession payload as an ordinary
    // region capture. Reuse the existing Copy handler so the first usable
    // long-shot result is immediately available to paste elsewhere.
    ActionRequest copy_request;
    copy_request.type = ActionType::Copy;
    const ActionResult copy_result = dispatcher_.dispatch(copy_request);
    if (!copy_result.ok) {
      MessageBoxW(tray_.hwnd(), L"Failed to copy the long screenshot.",
                  L"QingYing", MB_OK | MB_ICONERROR);
    }
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
      });

  if (!shown) {
    // Overlay not ready yet — keep integration path wired for when UI lands.
    recorded_owner_window_ = 0;
    pending_longshot_request_ = LongShotRequest{};
    return;
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
