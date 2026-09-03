#pragma once

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/capture_session.hpp"
#include "qingying/app/capture_workflow.hpp"
#include "qingying/app/browser_capture_receiver.hpp"
#include "qingying/app/hotkey_manager.hpp"
#include "qingying/app/longshot_controller.hpp"
#include "qingying/app/single_instance_guard.hpp"
#include "qingying/app/tray_controller.hpp"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/longshot/longshot_plugin_host.h"
#include "qingying/overlay/selection_overlay.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <Windows.h>

namespace qingying {

class Application {
 public:
  explicit Application(HINSTANCE instance);
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  int run();

 private:
  void registerHandlers();
  void installMessageRouter();
  void onCaptureHotkey();

  HINSTANCE instance_{nullptr};
  SingleInstanceGuard single_instance_;
  TrayController tray_;
  HotkeyManager hotkey_;
  ActionDispatcher dispatcher_;
  CaptureEngine capture_;
  LongShotPluginHost longshot_plugin_host_;
  LongShotEngine longshot_;
  ExportService export_service_;
  CaptureSession session_;
  PinManager pin_manager_;
  SelectionOverlay overlay_;
  LongShotController longshot_controller_;
  CaptureWorkflow capture_workflow_;
  BrowserCaptureReceiver browser_capture_receiver_;
};

}  // namespace qingying
