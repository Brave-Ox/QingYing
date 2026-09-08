#pragma once
#include "qingying/mcp/mcp_protocol_session.h"
namespace qingying {
// One connection-bound client; no ActionDispatcher or GUI dependency.
class McpBridge final {
 public:
  explicit McpBridge(std::shared_ptr<IAutomationClient> client, AutomationLimits limits = {});
  ~McpBridge();
  McpBridge(const McpBridge&) = delete;
  McpBridge& operator=(const McpBridge&) = delete;
  void receive(std::string_view line);
  std::optional<std::string> takeOutput();
  bool closed() const;
  void stop() noexcept;
  mcp::ClientInfo clientInfo() const;
 private:
  mcp::McpProtocolSession session_;
};
}  // namespace qingying
