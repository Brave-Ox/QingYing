#pragma once

#include <Windows.h>

#include <memory>
#include <string>

namespace qingying {

class Application {
 public:
  explicit Application(HINSTANCE instance, std::wstring test_namespace = {});
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;

  int run();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qingying
