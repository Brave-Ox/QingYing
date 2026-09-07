#include "qingying/app/application.hpp"

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/action_handlers.hpp"
#include "qingying/app/app_messages.hpp"
#include "qingying/app/capture_workflow.hpp"
#include "qingying/app/hotkey_manager.hpp"
#include "qingying/app/longshot_controller.hpp"
#include "qingying/app/result_action_service.h"
#include "qingying/app/result_store.h"
#include "qingying/app/single_instance_guard.hpp"
#include "qingying/app/tray_controller.hpp"
#include "qingying/automation/automation_endpoint.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/longshot/dll_longshot_profile.h"
#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/longshot/longshot_plugin_host.h"
#include "qingying/overlay/selection_overlay.hpp"
#include "qingying/pin/pin_manager.hpp"

#include "resource.h"

#include <memory>
#include <string>
#include <cstring>
#include <stdexcept>

namespace {

constexpr UINT_PTR kAutomationMaintenanceTimer = 0xF907;

qingying::ApplicationEpoch makeApplicationEpoch() {
  GUID guid{};
  if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("application epoch");
  qingying::ApplicationEpoch epoch{};
  std::memcpy(&epoch, &guid, sizeof(epoch));
  return epoch ? epoch : 1;
}

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

struct Application::Impl {
  explicit Impl(HINSTANCE instance)
      : instance_(instance),
        longshot_plugin_host_(makeLongShotPluginDirectory(instance)),
        longshot_(capture_, makeApplicationLongShotProfiles(
                               longshot_plugin_host_)),
        result_actions_(result_store_, export_service_, pin_manager_, {}, &interaction_gate_),
        longshot_controller_(longshot_, overlay_),
        capture_workflow_(dispatcher_, capture_, longshot_controller_,
                          result_store_, result_actions_, pin_manager_,
                          overlay_, &interaction_gate_),
        operation_registry_(makeApplicationEpoch()),
        scheduler_(
            [this](UINT message, UiMessageToken token) {
              return tray_.hwnd() && PostMessageW(tray_.hwnd(), message, 0,
                  static_cast<LPARAM>(token)) != FALSE;
            },
            [this](UiMessageToken ticket, const TrustedAutomationContext& context,
                   const AutomationRequest& request, std::shared_ptr<OperationControl> control) {
              automation_endpoint_.execute(ticket, context, request, std::move(control));
            }),
        automation_endpoint_(dispatcher_, capture_workflow_, result_store_,
            operation_registry_, scheduler_, interaction_gate_, {}, {}) {}

  ~Impl() {
    shutdown();
  }

  void shutdown() {
    if (stopping_) return;
    stopping_ = true;
    automation_endpoint_.shutdown();
    if (tray_.hwnd()) KillTimer(tray_.hwnd(), kAutomationMaintenanceTimer);
    hotkey_.unregisterAll(tray_.hwnd());
  }

  void registerHandlers() {
    registerAppHandlers(dispatcher_, capture_, result_store_, result_actions_);
    result_actions_.bindPinWindowActions();
  }

  void installMessageRouter() {
    tray_.setMessageFilter([this](UINT msg, WPARAM wparam, LPARAM lparam,
                                  LRESULT* result) -> bool {
      if (msg == WM_QINGYING_AUTOMATION_REQUEST || msg == WM_QINGYING_AUTOMATION_COMPLETE) {
        scheduler_.dispatch(msg, static_cast<UiMessageToken>(lparam));
        *result = 0;
        return true;
      }
      if (msg == WM_TIMER && wparam == kAutomationMaintenanceTimer) {
        automation_endpoint_.tick();
        *result = 0;
        return true;
      }
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
        // The tray window owns the process lifetime. CaptureWorkflow closes
        // any non-modal overlays and joins its worker before TrayController
        // posts quit.
        shutdown();
        return false;
      }
      if (msg == WM_ENDSESSION && wparam) {
        shutdown();
        return false;
      }
      if (msg == WM_QINGYING_LONGSHOT_COMPLETE) {
        capture_workflow_.handleLongShotCompletion(
            static_cast<UiMessageToken>(lparam));
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

  void onCaptureHotkey() {
    // Defer to message loop — hotkey handler must stay fast (≤300 ms path).
    PostMessageW(tray_.hwnd(), WM_QINGYING_BEGIN_CAPTURE, 0, 0);
  }

  int run() {
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
    capture_workflow_.setOwnerWindow(tray_.hwnd());
    result_actions_.setOwnerWindow(tray_.hwnd());

    installMessageRouter();
    if (!SetTimer(tray_.hwnd(), kAutomationMaintenanceTimer, 250, nullptr)) {
      shutdown();
      return 3;
    }

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

    shutdown();
    return static_cast<int>(msg.wParam);
  }

  HINSTANCE instance_{nullptr};
  SingleInstanceGuard single_instance_;
  TrayController tray_;
  HotkeyManager hotkey_;
  ActionDispatcher dispatcher_;
  CaptureEngine capture_;
  LongShotPluginHost longshot_plugin_host_;
  LongShotEngine longshot_;
  ExportService export_service_;
  ResultStore result_store_;
  PinManager pin_manager_;
  InteractionGate interaction_gate_;
  ResultActionService result_actions_;
  SelectionOverlay overlay_;
  LongShotController longshot_controller_;
  CaptureWorkflow capture_workflow_;
  OperationRegistry operation_registry_;
  UiActionScheduler scheduler_;
  AutomationEndpoint automation_endpoint_;
  bool stopping_{false};
};

Application::Application(HINSTANCE instance)
    : impl_(std::make_unique<Impl>(instance)) {}

Application::~Application() = default;

int Application::run() { return impl_->run(); }

}  // namespace qingying
