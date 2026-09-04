#pragma once

#include "qingying/action/action_dispatcher.hpp"

#include <memory>

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

  // The protocol layer hands the already-decoded typed request to this
  // boundary. It validates the schema and forwards only to ActionDispatcher;
  // no CaptureSession or GUI object is retained by MCP.
  ActionResult submit(const ActionRequest& request) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qingying
