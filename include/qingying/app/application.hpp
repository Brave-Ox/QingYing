#pragma once

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/single_instance_guard.hpp"
#include "qingying/app/tray_controller.hpp"

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

  HINSTANCE instance_{nullptr};
  SingleInstanceGuard single_instance_;
  TrayController tray_;
  ActionDispatcher dispatcher_;
};

}  // namespace qingying
