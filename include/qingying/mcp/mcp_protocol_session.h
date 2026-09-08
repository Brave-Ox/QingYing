#pragma once
#include "qingying/automation/automation_contract.h"
#include <memory>
#include <string_view>
namespace qingying::mcp {
struct ClientInfo {
  std::string name, version, requested_protocol, negotiated_protocol;
};
// Thread-safe. Input is one line without delimiter. Overflow closes admission;
// transport must call close() to reclaim the client. No stdio in callbacks.
class McpProtocolSession final {
 public:
  explicit McpProtocolSession(std::shared_ptr<IAutomationClient> client, AutomationLimits limits = {});
  ~McpProtocolSession();
  McpProtocolSession(const McpProtocolSession&) = delete;
  McpProtocolSession& operator=(const McpProtocolSession&) = delete;
  void receive(std::string_view line);
  std::optional<std::string> takeOutput();
  bool closed() const;
  void close() noexcept;
  ClientInfo clientInfo() const;
 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace qingying::mcp
