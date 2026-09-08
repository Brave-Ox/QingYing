#pragma once
#include "qingying/mcp/mcp_bridge.hpp"
#include <chrono>
namespace qingying::mcp {
struct StdioOptions {
  // Null selects inherited GetStdHandle; explicit handles are borrowed and
  // duplicated for this run. Supports synchronous anonymous pipes used by MCP.
  void* input{nullptr};
  void* output{nullptr};
  void* error{nullptr};
  std::size_t max_line_bytes{65536};
  std::chrono::milliseconds write_timeout{3000};
};
class StdioTransport final {
 public:
  explicit StdioTransport(StdioOptions options = {});
  // Blocking bridge-thread runner. EOF closes only this bridge/client. Every
  // blocking synchronous ReadFile/WriteFile belongs to a cancellable worker.
  bool run(McpBridge& bridge);
 private:
  StdioOptions options_;
};
}  // namespace qingying::mcp
