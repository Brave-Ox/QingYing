#include "qingying/mcp/mcp_bridge.hpp"

namespace qingying {

struct McpBridge::Impl {
  ActionDispatcher* dispatcher{nullptr};
};

McpBridge::McpBridge(ActionDispatcher* dispatcher) : impl_(new Impl) {
  impl_->dispatcher = dispatcher;
}

McpBridge::~McpBridge() {
  stop();
  delete impl_;
  impl_ = nullptr;
}

bool McpBridge::start() {
  return false;  // stub
}

void McpBridge::stop() {}

}  // namespace qingying
