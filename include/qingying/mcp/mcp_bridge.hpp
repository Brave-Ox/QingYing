#pragma once

#include "qingying/action/action_dispatcher.hpp"

namespace qingying {

// Named Pipe MCP bridge — PIMPL hides protocol details.
class McpBridge {
 public:
  explicit McpBridge(ActionDispatcher* dispatcher);
  ~McpBridge();

  McpBridge(const McpBridge&) = delete;
  McpBridge& operator=(const McpBridge&) = delete;

  bool start();
  void stop();

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace qingying
