#include "qingying/mcp/mcp_bridge.hpp"
namespace qingying {
McpBridge::McpBridge(std::shared_ptr<IAutomationClient> client, AutomationLimits limits)
    : session_(std::move(client), limits) {}
McpBridge::~McpBridge() { stop(); }
void McpBridge::receive(std::string_view line) { session_.receive(line); }
std::optional<std::string> McpBridge::takeOutput() { return session_.takeOutput(); }
bool McpBridge::closed() const { return session_.closed(); }
void McpBridge::stop() noexcept { session_.close(); }
mcp::ClientInfo McpBridge::clientInfo() const { return session_.clientInfo(); }
}  // namespace qingying
