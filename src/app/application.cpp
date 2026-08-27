#include "qingying/app/application.hpp"

#include "qingying/app/action_handlers.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/action/types.hpp"

#include "resource.h"

#include <commdlg.h>
#include <iterator>

namespace qingying {

Application::Application(HINSTANCE instance) : instance_(instance) {}

Application::~Application() {
  hotkey_.unregisterAll(tray_.hwnd());
}

void Application::registerHandlers() {
  registerAppHandlers(dispatcher_, capture_, export_service_, session_);
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
    return;
  }

  ActionRequest request;
  request.type = ActionType::Save;
  request.save_path = path;
  const ActionResult result = dispatcher_.dispatch(request);
  if (!result.ok) {
    MessageBoxW(tray_.hwnd(), L"Failed to save the latest capture.", L"QingYing",
                MB_OK | MB_ICONERROR);
  }
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

  ActionRequest capture_req;
  capture_req.type = ActionType::CaptureRegion;
  capture_req.x = region.x;
  capture_req.y = region.y;
  capture_req.width = region.width;
  capture_req.height = region.height;

  const ActionResult capture_result = dispatcher_.dispatch(capture_req);
  if (!capture_result.ok) {
    return;
  }

  if (region.action == SelectionAction::Save) {
    saveLastCapture();
    return;
  }

  ActionRequest copy_req;
  copy_req.type = ActionType::Copy;
  dispatcher_.dispatch(copy_req);
}

void Application::beginCaptureFlow() {
  const bool shown = overlay_.show([this](const SelectionResult& region) {
    runCapturePipeline(region);
  });

  if (!shown) {
    // Overlay not ready yet — keep integration path wired for when UI lands.
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
