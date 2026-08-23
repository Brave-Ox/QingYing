#pragma once

#include "qingying/action/i_action_handler.hpp"

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace qingying {

// Shared entry for GUI / command / MCP. Keep this header free of Win32/DXGI.
class ActionDispatcher {
 public:
  void registerHandler(std::unique_ptr<IActionHandler> handler);
  ActionResult dispatch(const ActionRequest& request) const;

 private:
  using Key = std::uint32_t;
  std::unordered_map<Key, std::unique_ptr<IActionHandler>> handlers_;
};

}  // namespace qingying
