#include "qingying/mcp/mcp_bridge.hpp"

#include <memory>

namespace qingying {

struct McpBridge::Impl {
  ActionDispatcher* dispatcher{nullptr};
};

McpBridge::McpBridge(ActionDispatcher* dispatcher)
    : impl_(std::make_unique<Impl>()) {
  impl_->dispatcher = dispatcher;
}

McpBridge::~McpBridge() { stop(); }

bool McpBridge::start() {
  return false;  // stub
}

void McpBridge::stop() {}

}  // namespace qingying
