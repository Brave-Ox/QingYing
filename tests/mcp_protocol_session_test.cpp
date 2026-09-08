#include "mcp_test_client.h"
using namespace qingying;
using namespace qingying::mcp;
using namespace qingying::mcp::test;
TEST(McpProtocolSessionTest, InitializationOrderAndVersionFallback) {
  McpBridge bridge(nullptr);
  bridge.receive(call()); EXPECT_EQ(take(bridge)["error"]["code"], -32600);
  bridge.receive(R"({"jsonrpc":"2.0","id":0,"method":"ping"})");
  EXPECT_EQ(take(bridge)["result"], Json::object());
  initialize(bridge, "2099-01-01");
  EXPECT_EQ(bridge.clientInfo().requested_protocol, "2099-01-01");
  EXPECT_EQ(bridge.clientInfo().version, "1.2.3");
  bridge.receive(R"({"jsonrpc":"2.0","id":1,"method":"initialize"})");
  EXPECT_EQ(take(bridge)["error"]["code"], -32600);
  bridge.receive(R"({"jsonrpc":"2.0","id":2,"method":"server/discover"})");
  EXPECT_EQ(take(bridge)["error"]["code"], -32601);
}
TEST(McpProtocolSessionTest, InitializedNotificationIsRequired) {
  McpBridge bridge(nullptr);
  bridge.receive(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"x","version":"y"}}})");
  take(bridge); bridge.receive(call());
  EXPECT_EQ(take(bridge)["error"]["code"], -32600);
}
TEST(McpProtocolSessionTest, NotificationsNeverInvokeToolsOrReply) {
  auto client = std::make_shared<Client>(); McpBridge bridge(client); initialize(bridge);
  for (const auto* method : {"ping", "tools/call", "initialize", "unknown", "notifications/cancelled"})
    bridge.receive(Json{{"jsonrpc", "2.0"}, {"method", method}}.dump());
  EXPECT_FALSE(bridge.takeOutput()); EXPECT_TRUE(client->requests.empty());
}
TEST(McpProtocolSessionTest, StringAndFullWidthIntegerIdsStayDistinct) {
  auto client = std::make_shared<Client>(); client->hold = true;
  McpBridge bridge(client); initialize(bridge);
  std::vector<Json> ids{UINT64_MAX, INT64_MIN, "18446744073709551615", ""};
  for (const auto& id : ids) bridge.receive(call(id));
  ASSERT_EQ(client->requests.size(), 4);
  for (std::size_t i = 0; i < ids.size(); ++i) {
    client->callbacks[i](Client::status(client->requests[i].request_id));
    EXPECT_EQ(take(bridge)["id"], ids[i]);
  }
}
TEST(McpProtocolSessionTest, DuplicateInflightIdClosesWithoutAmbiguousReply) {
  auto client = std::make_shared<Client>(); client->hold = true;
  McpBridge bridge(client); initialize(bridge);
  bridge.receive(call(1)); bridge.receive(call(1));
  EXPECT_TRUE(bridge.closed()); EXPECT_FALSE(bridge.takeOutput());
  EXPECT_EQ(client->requests.size(), 1);
}
TEST(McpProtocolSessionTest, CancellationUsesRequestIdBeforeOperationExists) {
  auto client = std::make_shared<Client>(); client->hold = true;
  McpBridge bridge(client); initialize(bridge); bridge.receive(call("pending"));
  bridge.receive(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":"pending"}})");
  ASSERT_EQ(client->requests.size(), 2);
  const auto& cancel = std::get<CancelOperationRequest>(client->requests[1].payload);
  EXPECT_EQ(std::get<RequestCancellation>(cancel.target).request_id, client->requests[0].request_id);
  EXPECT_NE(client->requests[1].request_id, client->requests[0].request_id);
  bridge.receive(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":"pending"}})");
  EXPECT_EQ(client->requests.size(), 2);
  client->callbacks[0](Client::status(client->requests[0].request_id));
  client->callbacks[1]({}); EXPECT_FALSE(bridge.takeOutput());
}
TEST(McpProtocolSessionTest, LateOrDuplicateCompletionCannotConsumeNewCall) {
  auto client = std::make_shared<Client>(); client->hold = true;
  McpBridge bridge(client); initialize(bridge); bridge.receive(call("reuse"));
  client->callbacks[0](Client::status(client->requests[0].request_id)); take(bridge);
  bridge.receive(call("reuse"));
  client->callbacks[0](Client::status(client->requests[0].request_id));
  EXPECT_FALSE(bridge.takeOutput());
  client->callbacks[1](Client::status(client->requests[1].request_id));
  EXPECT_EQ(take(bridge)["id"], "reuse");
}
TEST(McpProtocolSessionTest, ProtocolErrorsAreDistinctFromToolFailures) {
  McpBridge bridge(nullptr); initialize(bridge);
  bridge.receive(call(1, "capture_window")); EXPECT_EQ(take(bridge)["error"]["code"], -32602);
  bridge.receive(call(2, "get_operation", {{"operation_id", 12}})); EXPECT_EQ(take(bridge)["error"]["code"], -32602);
  bridge.receive(call(3, "get_operation", {{"operation_id", "opaque"}}));
  auto result = take(bridge); EXPECT_FALSE(result.contains("error"));
  EXPECT_EQ(result["result"]["isError"], true);
}
TEST(McpProtocolSessionTest, MalformedUnicodeDuplicatesDepthAndIdsAreRejected) {
  McpBridge bridge(nullptr);
  for (const std::string& invalid : {std::string("{"), std::string("{\"x\":1,\"x\":2}"),
      std::string(17, '[') + std::string(17, ']'), std::string("{\"x\":\"") + '\xff' + "\"}"}) {
    bridge.receive(invalid); EXPECT_EQ(take(bridge)["error"]["code"], -32700);
  }
  for (const auto& id : std::vector<Json>{nullptr, true, 1.5}) {
    bridge.receive(Json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "ping"}}.dump());
    auto response = take(bridge); EXPECT_EQ(response["error"]["code"], -32600);
    EXPECT_TRUE(response["id"].is_null());
  }
  bridge.receive("[]"); EXPECT_EQ(take(bridge)["error"]["code"], -32600);
}
TEST(McpProtocolSessionTest, OutputAndInflightQueuesAreBounded) {
  AutomationLimits limits; limits.max_output_frames_per_connection = 1;
  McpBridge bridge(nullptr, limits);
  bridge.receive(R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
  bridge.receive(R"({"jsonrpc":"2.0","id":2,"method":"ping"})");
  EXPECT_TRUE(bridge.closed()); EXPECT_FALSE(bridge.takeOutput());
  auto client = std::make_shared<Client>(); client->hold = true;
  McpBridge pending(client); initialize(pending);
  for (int id = 0; id < 5; ++id) pending.receive(call(id));
  EXPECT_EQ(client->requests.size(), 4);
  EXPECT_EQ(take(pending)["result"]["structuredContent"]["error"]["code"], "ResourceLimit");
}
TEST(McpProtocolSessionTest, CloseSuppressesPendingResponseAndClosesOnlyBoundClient) {
  auto client = std::make_shared<Client>(); client->hold = true;
  auto other = std::make_shared<Client>(); McpBridge bridge(client), independent(other);
  initialize(bridge); bridge.receive(call()); bridge.stop();
  EXPECT_FALSE(bridge.takeOutput()); EXPECT_EQ(client->closes, 1); EXPECT_EQ(other->closes, 0);
}
