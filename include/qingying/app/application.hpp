#pragma once

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/capture_session.hpp"
#include "qingying/app/hotkey_manager.hpp"
#include "qingying/app/single_instance_guard.hpp"
#include "qingying/app/tray_controller.hpp"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/overlay/selection_overlay.hpp"

#include <Windows.h>

#include <functional>

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
  void beginCaptureFlow();
  void runCapturePipeline(const SelectionResult& region);

  HINSTANCE instance_{nullptr};
  SingleInstanceGuard single_instance_;
  TrayController tray_;
  HotkeyManager hotkey_;
  ActionDispatcher dispatcher_;
  CaptureEngine capture_;
  ExportService export_service_;
  CaptureSession session_;
  SelectionOverlay overlay_;
};

}  // namespace qingying
