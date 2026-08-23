#pragma once

#include "qingying/action/action_dispatcher.hpp"

namespace qingying {

class Application {
 public:
  Application();
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  int run();

 private:
  void registerHandlers();

  ActionDispatcher dispatcher_;
};

}  // namespace qingying
