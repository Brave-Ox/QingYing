#pragma once
#include "qingying/mcp/mcp_bridge.hpp"
#include "tool_catalog.h"
#include <gtest/gtest.h>
namespace qingying::mcp::test {
class Client : public IAutomationClient {
 public:
  bool hold{false}, throws{false};
  int closes{0};
  std::vector<AutomationRequest> requests;
  std::vector<AutomationCompletion> callbacks;
  void submit(AutomationRequest request, AutomationCompletion completion) override {
    if (throws) throw std::runtime_error("fake unavailable");
    requests.push_back(request); callbacks.push_back(completion);
    if (!hold) completion(status(request.request_id));
  }
  static AutomationResponse status(RequestId id) {
    AutomationResponse response;
    response.connection = {17, 1};
    response.result.request_id = id; response.result.ok = true;
    response.result.error_code = ErrorCode::kOk;
    StatusInfo info; info.reachable = true; info.app_running = true;
    info.automation_enabled = true; info.limits = AutomationLimits{};
    response.result.output = info; return response;
  }
  void close() noexcept override {
    ++closes;
    auto pending = std::move(callbacks); callbacks.clear();
    for (std::size_t i = 0; i < pending.size(); ++i) {
      AutomationResponse response; response.result.request_id = requests[i].request_id;
      response.result.error_code = ErrorCode::kCancelled; pending[i](response);
    }
  }
};
inline Json take(McpBridge& bridge) {
  auto line = bridge.takeOutput();
  EXPECT_TRUE(line.has_value());
  return line ? Json::parse(*line) : Json();
}
inline void initialize(McpBridge& bridge, const std::string& version = "2025-11-25") {
  bridge.receive(Json{{"jsonrpc", "2.0"}, {"id", "init"}, {"method", "initialize"},
      {"params", {{"protocolVersion", version}, {"capabilities", Json::object()},
      {"clientInfo", {{"name", "fixture"}, {"version", "1.2.3"}}}}}}.dump());
  EXPECT_EQ(take(bridge)["result"]["protocolVersion"], "2025-11-25");
  bridge.receive(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
}
inline std::string call(Json id = 1, std::string name = "status", Json arguments = Json::object()) {
  return Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"},
      {"params", {{"name", name}, {"arguments", arguments}}}}.dump();
}
}  // namespace qingying::mcp::test
