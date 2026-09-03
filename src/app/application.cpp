#include "qingying/app/application.hpp"

#include "qingying/app/action_handlers.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/longshot/dll_longshot_profile.h"

#include "resource.h"

#include <string>

namespace {

std::wstring makeLongShotPluginDirectory(HINSTANCE instance) {
  constexpr DWORD kPathCapacity = 32768;
  wchar_t path[kPathCapacity] = {};
  const HINSTANCE module =
      instance != nullptr ? instance : GetModuleHandleW(nullptr);
  const DWORD length = GetModuleFileNameW(module, path, kPathCapacity);
  if (length == 0 || length >= kPathCapacity) {
    return {};
  }

  const std::wstring executable_path(path, length);
  const std::wstring::size_type separator =
      executable_path.find_last_of(L"\\/");
  if (separator == std::wstring::npos) {
    return {};
  }
  return executable_path.substr(0, separator + 1) +
         L"plugins\\longshot";
}

qingying::LongShotProfileRegistry makeApplicationLongShotProfiles(
    qingying::LongShotPluginHost& plugin_host) {
  (void)plugin_host.loadDirectory();

  qingying::LongShotProfileRegistry profiles;
  (void)qingying::addDllLongShotProfiles(plugin_host, profiles);
  return profiles;
}

}  // namespace

namespace qingying {

Application::Application(HINSTANCE instance)
    : instance_(instance),
      longshot_plugin_host_(makeLongShotPluginDirectory(instance)),
      longshot_(capture_, makeApplicationLongShotProfiles(
                             longshot_plugin_host_)),
      result_actions_(result_store_, export_service_, pin_manager_),
      longshot_controller_(longshot_, overlay_),
      capture_workflow_(dispatcher_, capture_, longshot_controller_,
                        result_store_, result_actions_, pin_manager_, overlay_) {}

Application::~Application() {
  capture_workflow_.shutdown();
  hotkey_.unregisterAll(tray_.hwnd());
}

void Application::registerHandlers() {
  registerAppHandlers(dispatcher_, capture_, result_store_, result_actions_);
  result_actions_.bindPinWindowActions();
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
      (void)capture_workflow_.beginSelection();
      *result = 0;
      return true;
    }
    if (msg == WM_DESTROY) {
      // The tray window owns the process lifetime. CaptureWorkflow closes any
      // non-modal overlays and joins its worker before TrayController posts
      // quit.
      capture_workflow_.shutdown();
      return false;
    }
    if (msg == WM_QINGYING_LONGSHOT_COMPLETE) {
      capture_workflow_.handleLongShotCompletion(
          static_cast<std::intptr_t>(lparam));
      *result = 0;
      return true;
    }
    if (msg == WM_QINGYING_WORKFLOW_CONTINUE) {
      capture_workflow_.continueWorkflow();
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
  capture_workflow_.setOwnerWindow(
      reinterpret_cast<std::uintptr_t>(tray_.hwnd()));
  result_actions_.setOwnerWindow(
      reinterpret_cast<std::uintptr_t>(tray_.hwnd()));

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
