#include "qingying/mcp/mcp_bridge.hpp"

#include <memory>

namespace qingying {

namespace {

ActionResult bridgeFailure(const ActionRequest& request, int error_code,
                           const std::string& message) {
  ActionResult result;
  result.request_id = request.request_id;
  result.operation_id = request.operation_id;
  result.ok = false;
  result.error_code = error_code;
  result.message = message;
  return result;
}

}  // namespace

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

ActionResult McpBridge::submit(const ActionRequest& request) const {
  if (impl_ == nullptr || impl_->dispatcher == nullptr) {
    return bridgeFailure(request, ErrorCode::kNotReady,
                         "MCP action dispatcher is not available");
  }

  const ActionValidationResult validation = validateActionRequest(request);
  if (!validation.valid) {
    return bridgeFailure(request, ErrorCode::kInvalidArgument,
                         validation.message);
  }
  return impl_->dispatcher->dispatch(request);
}

}  // namespace qingying
