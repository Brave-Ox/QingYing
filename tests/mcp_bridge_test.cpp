#include "mcp_test_client.h"
using namespace qingying;
using namespace qingying::mcp;
using namespace qingying::mcp::test;
TEST(McpBridgeTest, MissingPipeStatusDoesNotClaimApplicationStopped) {
  McpBridge bridge(nullptr); initialize(bridge); bridge.receive(call());
  auto output = take(bridge)["result"];
  EXPECT_FALSE(output["isError"].get<bool>());
  EXPECT_EQ(output["structuredContent"]["reachable"], false);
  EXPECT_TRUE(output["structuredContent"]["app_running"].is_null());
  EXPECT_EQ(output["structuredContent"]["connection_reason"], "NotReady");
}
TEST(McpBridgeTest, SyncCompletionHasRegisteredCorrelation) {
  auto client = std::make_shared<Client>(); McpBridge bridge(client); initialize(bridge);
  bridge.receive(call("original"));
  EXPECT_EQ(take(bridge)["id"], "original");
  ASSERT_EQ(client->requests.size(), 1);
  EXPECT_NE(client->requests[0].request_id, 0);
  bridge.stop(); bridge.stop(); EXPECT_EQ(client->closes, 1);
}
TEST(McpBridgeTest, ThrowsBecomeUnavailableAndInvalidArgumentsDoNotSubmit) {
  auto client = std::make_shared<Client>(); client->throws = true;
  McpBridge bridge(client); initialize(bridge); bridge.receive(call());
  EXPECT_EQ(take(bridge)["result"]["structuredContent"]["reachable"], false);
  bridge.receive(call(2, "status", {{"extra", true}}));
  EXPECT_EQ(take(bridge)["error"]["code"], -32602);
  EXPECT_TRUE(client->requests.empty());
}
TEST(McpBridgeTest, DisconnectedStatusRetainsIdentityButReportsUnreachable) {
  auto client = std::make_shared<Client>(); client->hold = true;
  McpBridge bridge(client); initialize(bridge); bridge.receive(call());
  auto response = Client::status(client->requests[0].request_id);
  response.result.ok = false; response.result.error_code = ErrorCode::kCancelled;
  response.transport_available = false;
  client->callbacks[0](response);
  const auto output = take(bridge)["result"];
  EXPECT_EQ(output["isError"], false);
  EXPECT_EQ(output["structuredContent"]["reachable"], false);
  EXPECT_TRUE(output["structuredContent"]["app_running"].is_null());
}
