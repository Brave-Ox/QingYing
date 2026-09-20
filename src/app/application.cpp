#include "qingying/app/application.hpp"

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/diagnostics/fault_boundary.h"
#include "qingying/app/action_handlers.hpp"
#include "qingying/app/application_shutdown_coordinator.h"
#include "qingying/app/automation_runtime.h"
#include "qingying/app/automation_settings.h"
#include "qingying/app/app_messages.hpp"
#include "qingying/app/ui_message_channel.h"
#include "qingying/app/capture_workflow.hpp"
#include "qingying/app/capture_service.h"
#include "qingying/app/export_executor.h"
#include "qingying/app/hotkey_manager.hpp"
#include "qingying/app/longshot_controller.hpp"
#include "qingying/app/result_action_service.h"
#include "qingying/app/result_store.h"
#include "qingying/app/save_policy.h"
#include "qingying/app/single_instance_guard.hpp"
#include "qingying/app/tray_controller.hpp"
#include "qingying/automation/automation_endpoint.h"
#include "qingying/app/automation_workflow_adapter.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/longshot/dll_longshot_profile.h"
#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/longshot/longshot_plugin_host.h"
#include "qingying/overlay/selection_overlay.hpp"
#include "qingying/pin/pin_manager.hpp"
#include "qingying/ui/modern_toolbar.hpp"

#include "resource.h"

#include <memory>
#include <chrono>
#include <string>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <ShlObj.h>

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

void reportShutdownDiagnostic(
    const qingying::ApplicationShutdownDiagnostic& diagnostic) noexcept {
  if (diagnostic.completed && !diagnostic.deadline_exceeded) {
    return;
  }
  std::string message = "[QingYing shutdown] phase=";
  message += qingying::applicationShutdownPhaseName(diagnostic.phase);
  message += " participant=";
  message += diagnostic.participant;
  message += " elapsed_ms=";
  message += std::to_string(diagnostic.elapsed.count());
  message += " budget_ms=";
  message += std::to_string(diagnostic.budget.count());
  message += " remaining_before_ms=";
  message += std::to_string(diagnostic.remaining_before.count());
  if (diagnostic.deadline_exceeded) message += " deadline_exceeded";
  if (!diagnostic.detail.empty()) {
    message += " detail=";
    message += diagnostic.detail;
  }
  message += "\n";
  OutputDebugStringA(message.c_str());
}

}  // namespace

namespace qingying {

struct Application::Impl {
  explicit Impl(HINSTANCE instance, std::wstring test_namespace)
      : instance_(instance),
        test_namespace_(std::move(test_namespace)),
        automation_settings_(test_namespace_),
        single_instance_(test_namespace_.empty() ? L"Local\\QingYing.SingleInstance" :
            (L"Local\\QingYing.Test." + test_namespace_).c_str()),
        longshot_plugin_host_(makeLongShotPluginDirectory(instance)),
        longshot_(capture_, makeApplicationLongShotProfiles(
                               longshot_plugin_host_)),
        save_policy_(automation_settings_.allowedSaveDirectories()),
        result_actions_(result_store_, export_service_, pin_manager_, {},
                        &interaction_gate_, &save_policy_),
        export_executor_(AutomationLimits{}.max_queued_exports),
        capture_service_(capture_, result_store_, pin_manager_, interaction_gate_),
        longshot_controller_(longshot_, overlay_),
        capture_workflow_(capture_, capture_service_, longshot_controller_,
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
        automation_adapter_(dispatcher_, capture_workflow_, result_store_),
        automation_endpoint_(automation_adapter_, automation_adapter_,
            operation_registry_, scheduler_, interaction_gate_, {},
            AutomationEndpoint::ExecutionPolicy{
                automationReadyActions(),
                [this] {
                  export_executor_.requestStop();
                  capture_service_.beginStop();
                }, false,
                [this] {
                  const auto usage = pin_manager_.agentUsage();
                  return std::make_pair(usage.count, usage.bytes);
                }}),
        automation_runtime_(automation_endpoint_, scheduler_, [this] {
          ipc::PipeOptions options;
          options.test_suffix = test_namespace_;
          return options;
        }()),
        shutdown_coordinator_(
            {
                {ApplicationShutdownPhase::StopAdmission,
                 "automation_transport",
                 [this](ApplicationShutdownDeadline) {
                   automation_runtime_.stopAccepting();
                   return true;
                 },
                 [this] {
                   return automation_runtime_.enabled()
                              ? std::string("pipe=accepting_stopped")
                              : std::string("pipe=not_running");
                 }},
                {ApplicationShutdownPhase::StopAdmission,
                 "tray_input",
                 [this](ApplicationShutdownDeadline) {
                   if (tray_.hwnd()) {
                     KillTimer(tray_.hwnd(), kAutomationMaintenanceTimer);
                   }
                   hotkey_.unregisterAll(tray_.hwnd());
                   return true;
                 },
                 [] { return std::string("hotkey_and_timer=stopped"); }},
                {ApplicationShutdownPhase::RejectNewWork,
                 "automation_endpoint",
                 [this](ApplicationShutdownDeadline) {
                   automation_endpoint_.beginShutdown();
                   return true;
                 },
                 [] { return std::string("endpoint=rejecting_new_work"); }},
                {ApplicationShutdownPhase::RejectNewWork,
                 "capture_workflow",
                 [this](ApplicationShutdownDeadline) {
                   capture_workflow_.beginShutdown();
                   return true;
                 },
                 [this] {
                   return capture_workflow_.active()
                              ? std::string("workflow=stopping")
                              : std::string("workflow=idle");
                 }},
                {ApplicationShutdownPhase::CancelAndWait,
                 "business_producers",
                 [this](ApplicationShutdownDeadline) {
                   automation_endpoint_.stopBusinessProducers();
                   return true;
                 },
                 [this] {
                   return "export_queued=" +
                          std::to_string(export_executor_.queued()) +
                          " export_running=" +
                          std::to_string(export_executor_.running());
                 }},
                {ApplicationShutdownPhase::CancelAndWait,
                 "longshot_worker",
                 [this](ApplicationShutdownDeadline deadline) {
                   return capture_workflow_.joinLongShotUntil(deadline);
                 },
                 [this] {
                   return capture_workflow_.longShotDiagnosticSnapshot();
                 },
                 std::chrono::milliseconds(1500)},
                {ApplicationShutdownPhase::CancelAndWait,
                 "uia_query_worker",
                 [this](ApplicationShutdownDeadline deadline) {
                   return capture_workflow_.joinUiaUntil(deadline);
                 },
                 [this] {
                   return capture_workflow_.uiaDiagnosticSnapshot();
                 },
                 std::chrono::milliseconds(1000)},
                {ApplicationShutdownPhase::CancelAndWait,
                 "capture_worker",
                 [this](ApplicationShutdownDeadline deadline) {
                   capture_service_.beginStop();
                   return capture_service_.joinUntil(deadline);
                 },
                 [this] { return capture_service_.diagnosticSnapshot(); },
                 std::chrono::milliseconds(1000)},
                {ApplicationShutdownPhase::CancelAndWait,
                 "export_worker",
                 [this](ApplicationShutdownDeadline deadline) {
                   return export_executor_.joinUntil(deadline);
                 },
                 [this] {
                   return export_executor_.diagnosticSnapshot();
                 },
                 std::chrono::milliseconds(1500)},
                {ApplicationShutdownPhase::CancelAndWait,
                 "pipe_workers",
                 [this](ApplicationShutdownDeadline deadline) {
                   return automation_runtime_.shutdownTransportUntil(deadline);
                 },
                 [this] {
                   return automation_runtime_.transportDiagnosticSnapshot();
                 },
                 std::chrono::milliseconds(1000)},
                {ApplicationShutdownPhase::DrainAndDestroy,
                 "capture_callbacks",
                 [this](ApplicationShutdownDeadline) {
                   capture_workflow_.finishShutdown();
                   return true;
                 },
                 [] { return std::string("capture_callbacks=drained"); }},
                {ApplicationShutdownPhase::DrainAndDestroy,
                 "automation_endpoint_callbacks",
                 [this](ApplicationShutdownDeadline) {
                   automation_endpoint_.finishShutdown();
                   return true;
                 },
                 [this] {
                   return "scheduler_pending=" +
                          std::to_string(scheduler_.pending());
                 }},
            },
            ApplicationShutdownCoordinator::Options{
                std::chrono::milliseconds(5000), reportShutdownDiagnostic}) {}

  ~Impl() {
    shutdown();
    tray_.setMessageFilter({});
    tray_.setAutomationToggle({});
    tray_.destroy();
  }

  void shutdown() {
    if (stopping_) return;
    stopping_ = true;
    const auto report = shutdown_coordinator_.shutdown();
    shutdown_requires_process_reclaim_ = !report.completed;
  }

  bool requiresProcessReclaim() const noexcept {
    return shutdown_requires_process_reclaim_;
  }

  void registerHandlers() {
    registerAppHandlers(dispatcher_, capture_service_, result_store_,
                        result_actions_, &export_executor_);
    result_actions_.setExportExecutor(export_executor_, [this](ActionTask task) {
      const auto token = result_action_messages_.push(std::move(task));
      if (token && !PostMessageW(tray_.hwnd(), WM_QINGYING_RESULT_ACTION_COMPLETE, 0,
                                static_cast<LPARAM>(*token))) {
        result_action_messages_.discard(*token);
        OutputDebugStringW(L"QingYing could not post save completion\n");
      }
    });
    result_actions_.bindPinWindowActions();
  }

  void installMessageRouter() {
    tray_.setMessageFilter([this](UINT msg, WPARAM wparam, LPARAM lparam,
                                  LRESULT* result) -> bool {
      if (msg == WM_QINGYING_RESULT_ACTION_COMPLETE) {
        auto task = result_action_messages_.take<ActionTask>(static_cast<UiMessageToken>(lparam));
        if (task && *task) containFault(FaultOrigin::Ui, FaultDomain::Request, [&] { (*task)(); });
        *result = 0;
        return true;
      }
      if (msg == WM_QINGYING_AUTOMATION_WAKE ||
          msg == WM_QINGYING_AUTOMATION_REQUEST || msg == WM_QINGYING_AUTOMATION_COMPLETE) {
        if (msg == WM_QINGYING_AUTOMATION_WAKE) automation_runtime_.drainTransport();
        scheduler_.dispatch(msg, static_cast<UiMessageToken>(lparam));
        *result = 0;
        return true;
      }
      if (msg == WM_TIMER && wparam == kAutomationMaintenanceTimer) {
        automation_runtime_.tick();
        hotkey_.maintenance(tray_.hwnd());
        *result = 0;
        return true;
      }
      if (msg == WM_HOTKEY &&
          hotkey_.isCurrentCaptureHotkeyId(static_cast<int>(wparam))) {
        onCaptureHotkey();
        *result = 0;
        return true;
      }
      if (msg == WM_QINGYING_BEGIN_CAPTURE) {
        (void)capture_workflow_.beginSelection();
        *result = 0;
        return true;
      }
      if (msg == WM_CLOSE || (msg == WM_ENDSESSION && wparam)) {
        shutdown();
        tray_.destroy();
        *result = 0;
        return true;
      }
      if (msg == WM_QUERYENDSESSION) {
        *result = TRUE;
        return true;
      }
      if (msg == WM_DESTROY) {
        // The tray window owns the process lifetime. CaptureWorkflow closes
        // any non-modal overlays and joins its worker before TrayController
        // posts quit.
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
      if (!test_namespace_.empty()) return 1;
      MessageBoxW(nullptr,
                  L"QingYing is already running in the system tray.",
                  L"QingYing", MB_OK | MB_ICONINFORMATION);
      return 1;
    }

    // 工具栏图标首次使用会触发 GDI+ 初始化。在托盘应用尚未进入交互
    // 消息循环时预热，避免把这笔一次性成本落在框选松手的关键路径上。
    static_cast<void>(prepareModernToolbarRendering());

    registerHandlers();

    if (!tray_.create(instance_)) {
      MessageBoxW(nullptr, L"Failed to create system tray icon.", L"QingYing",
                  MB_OK | MB_ICONERROR);
      return 2;
    }
    capture_workflow_.setOwnerWindow(tray_.hwnd());
    result_actions_.setOwnerWindow(tray_.hwnd());

    installMessageRouter();
    tray_.setAutomationToggle([this](bool enabled) {
      if (enabled && !automation_runtime_.enable()) {
        MessageBoxW(tray_.hwnd(), L"无法启动本机 Agent 接口。", L"QingYing", MB_OK | MB_ICONERROR);
        return false;
      }
      if (!enabled) automation_runtime_.disable();
      if (!automation_settings_.setEnabled(enabled)) {
        if (enabled) automation_runtime_.disable();
        MessageBoxW(tray_.hwnd(), L"无法保存本机 Agent 接口设置。", L"QingYing", MB_OK | MB_ICONERROR);
        return !enabled;
      }
      return true;
    });
    if (!SetTimer(tray_.hwnd(), kAutomationMaintenanceTimer, 1000, nullptr)) {
      shutdown();
      return 3;
    }

    const ShortcutBinding capture_hotkey{
        HotkeyDefaults::kCaptureModifiers,
        test_namespace_.empty() ? HotkeyDefaults::kCaptureVirtualKey : VK_F24};
    if (!hotkey_.registerCaptureHotkey(tray_.hwnd(), capture_hotkey)) {
      if (!test_namespace_.empty()) { shutdown(); return 5; }
      MessageBoxW(
          tray_.hwnd(),
          L"Failed to register capture hotkey (Ctrl+Shift+Q).\n"
          L"It may be used by another application.",
          L"QingYing", MB_OK | MB_ICONWARNING);
    }

    if (automation_settings_.enabled()) {
      const bool enabled = automation_runtime_.enable();
      tray_.setAutomationEnabled(enabled);
      if (!enabled) MessageBoxW(tray_.hwnd(), L"无法启动本机 Agent 接口。", L"QingYing", MB_OK | MB_ICONWARNING);
    }

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }

    shutdown();
    tray_.destroy();
    return static_cast<int>(msg.wParam);
  }

  HINSTANCE instance_{nullptr};
  std::wstring test_namespace_;
  AutomationSettings automation_settings_;
  SingleInstanceGuard single_instance_;
  TrayController tray_;
  HotkeyManager hotkey_;
  ActionDispatcher dispatcher_;
  CaptureEngine capture_;
  LongShotPluginHost longshot_plugin_host_;
  LongShotEngine longshot_;
  ExportService export_service_;
  SavePolicy save_policy_;
  ResultStore result_store_;
  PinManager pin_manager_;
  InteractionGate interaction_gate_;
  ResultActionService result_actions_;
  UiMessageChannel result_action_messages_;
  ExportExecutor export_executor_;
  CaptureService capture_service_;
  SelectionOverlay overlay_;
  LongShotController longshot_controller_;
  CaptureWorkflow capture_workflow_;
  OperationRegistry operation_registry_;
  UiActionScheduler scheduler_;
  AutomationWorkflowAdapter automation_adapter_;
  AutomationEndpoint automation_endpoint_;
  AutomationRuntime automation_runtime_;
  ApplicationShutdownCoordinator shutdown_coordinator_;
  bool stopping_{false};
  bool shutdown_requires_process_reclaim_{false};
};

Application::Application(HINSTANCE instance, std::wstring test_namespace)
    : impl_(std::make_unique<Impl>(instance, std::move(test_namespace))) {}

Application::~Application() {
  if (!impl_) return;
  impl_->shutdown();
  if (impl_->requiresProcessReclaim()) {
    // A timed-out worker still owns callbacks and context. Keep the complete
    // application graph alive until process teardown instead of destroying a
    // live joinable thread's owner.
    (void)impl_.release();
    return;
  }
  impl_.reset();
}

int Application::run() { return impl_->run(); }

}  // namespace qingying
