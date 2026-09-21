#pragma once

#include <Windows.h>

#include <functional>
#include <string>

namespace qingying {

// Small modeless entry point for local commands. Execution remains in
// Application, so this window never owns ActionDispatcher or workflow state.
class CommandWindow final {
 public:
  using ExecuteCallback = std::function<std::wstring(const std::wstring&)>;

  CommandWindow() = default;
  ~CommandWindow();

  CommandWindow(const CommandWindow&) = delete;
  CommandWindow& operator=(const CommandWindow&) = delete;

  bool show(HWND owner, ExecuteCallback execute);
  void close() noexcept;
  bool visible() const noexcept;

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                  LPARAM lparam);
  LRESULT handleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  bool create(HWND owner);
  void layout(int width, int height) noexcept;
  void executeCurrentCommand();

  HWND hwnd_{nullptr};
  HWND edit_{nullptr};
  HWND status_{nullptr};
  ExecuteCallback execute_;
};

}  // namespace qingying
