#pragma once

#include <memory>

#include <Windows.h>

#include "qingying/app/settings_application_service.hpp"

namespace qingying {

class SettingsWindow final
{
 public:
  explicit SettingsWindow(SettingsApplicationService& service);
  ~SettingsWindow();

  SettingsWindow(const SettingsWindow&) = delete;
  SettingsWindow& operator=(const SettingsWindow&) = delete;

  bool show(HWND owner, const SettingsState& initial_state);
  void activate() noexcept;
  void close() noexcept;
  void refreshExternalState(const SettingsState& state);
  bool visible() const noexcept;

 private:
  struct Impl;

  std::unique_ptr<Impl> m_impl;
};

}  // namespace qingying
