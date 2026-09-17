#pragma once

#include "qingying/action/i_async_action_handler.h"
#include "qingying/action/i_action_handler.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace qingying {

// Shared entry for GUI / command / MCP. Keep this header free of Win32/DXGI.
class ActionDispatcher {
 public:
  explicit ActionDispatcher(ActionExecutor executor = {});

  void registerHandler(std::unique_ptr<IActionHandler> handler);
  void registerAsyncHandler(std::unique_ptr<IAsyncActionHandler> handler);
  void setExecutor(ActionExecutor executor);
  bool hasHandler(ActionType type) const noexcept;
  ActionResult dispatch(const ActionRequest& request) const;

  // Completes synchronously for a registered IActionHandler. An
  // IAsyncActionHandler receives the injected executor and may complete later.
  // Every non-empty completion is delivered at most once, including failures
  // caused by validation, cancellation, missing handlers, or exceptions.
  void submit(const ActionRequest& request, ActionCompletion completion) const;

 private:
  using Key = std::uint32_t;
  std::unordered_map<Key, std::unique_ptr<IActionHandler>> handlers_;
  std::unordered_map<Key, std::unique_ptr<IAsyncActionHandler>> async_handlers_;
  ActionExecutor executor_;
};

}  // namespace qingying
