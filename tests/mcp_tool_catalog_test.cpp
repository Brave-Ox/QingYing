#include "mcp_test_client.h"
#include "automation_wire_codec.h"
using namespace qingying;
using namespace qingying::mcp;
using namespace qingying::mcp::test;
TEST(McpToolCatalogTest, OnlyFourRegisteredToolsAndDescriptorsMatchDecoders) {
  ASSERT_EQ(tools().size(), 4);
  for (const auto& tool : tools()) {
    const auto descriptor = tool.descriptor({});
    EXPECT_EQ(descriptor["name"], tool.name);
    EXPECT_FALSE(descriptor["inputSchema"]["additionalProperties"].get<bool>());
    Json arguments = Json::object();
    if (*tool.argument) arguments[tool.argument] = "opaque_123";
    auto request = tool.decode(arguments, 1, {});
    EXPECT_TRUE(validateAutomationRequest(request).valid);
    arguments["extra"] = 1;
    EXPECT_THROW(tool.decode(arguments, 1, {}), std::invalid_argument);
  }
  EXPECT_EQ(findTool("save"), nullptr);
}
TEST(McpToolCatalogTest, HandleSchemaAndDecoderShareExactLengthAndAlphabet) {
  AutomationLimits limits; limits.max_opaque_handle_bytes = 4;
  for (const auto& tool : tools()) if (*tool.argument) {
    EXPECT_EQ(tool.descriptor(limits)["inputSchema"]["properties"][tool.argument]["maxLength"], 4);
    EXPECT_NO_THROW(tool.decode({{tool.argument, "a_-9"}}, 1, limits));
    for (const auto& value : std::vector<Json>{"", "abcde", "a.b", "中文", 12, nullptr, true})
      EXPECT_THROW(tool.decode({{tool.argument, value}}, 1, limits), std::invalid_argument);
  }
}
TEST(McpToolCatalogTest, OpaqueRequestsRoundtripPrivateWireWithoutNumericConversion) {
  for (const auto& tool : tools()) if (*tool.argument) {
    auto request = tool.decode({{tool.argument, "18446744073709551616"}}, 1, {});
    ipc::WireRequest wire; wire.request = request;
    auto encoded = ipc::encodeFrame(wire); ASSERT_TRUE(encoded);
    auto decoded = ipc::decodeBody(std::string_view(encoded.bytes).substr(4)); ASSERT_TRUE(decoded);
    const auto& payload = std::get<ipc::WireRequest>(*decoded.message).request.payload;
    if (auto* get = std::get_if<GetOperationRequest>(&payload)) EXPECT_EQ(get->operation_handle->value, "18446744073709551616");
    if (auto* cancel = std::get_if<CancelOperationRequest>(&payload)) EXPECT_EQ(cancel->operation_handle->value, "18446744073709551616");
    if (auto* release = std::get_if<ReleaseResultRequest>(&payload)) EXPECT_EQ(release->result_handle->value, "18446744073709551616");
  }
}
TEST(McpToolCatalogTest, ReleaseOutputReturnsOpaqueHandleAndMatchingText) {
  auto response = Client::status(1); response.result.output = std::monostate{};
  response.control = ReleasedResult{1234, true};
  const auto result = findTool("release_result")->encode(response, {{"result_id", "opaque"}}, {});
  EXPECT_EQ(result["structuredContent"]["result_id"], "opaque");
  EXPECT_EQ(Json::parse(result["content"][0]["text"].get<std::string>()), result["structuredContent"]);
  EXPECT_EQ(result["isError"], false);
}
TEST(McpToolCatalogTest, NumericAndOpaqueTargetsCannotBeCombinedOrSpoofTransportFacts) {
  for (AutomationPayload payload : std::vector<AutomationPayload>{
      GetOperationRequest{9, OperationHandle{"opaque"}},
      CancelOperationRequest{RequestCancellation{9}, OperationHandle{"opaque"}},
      ReleaseResultRequest{9, ResultHandle{"opaque"}}}) {
    AutomationRequest request; request.request_id = 1; request.payload = payload;
    EXPECT_FALSE(validateAutomationRequest(request).valid);
    ipc::WireRequest wire; wire.request = request; EXPECT_FALSE(ipc::encodeFrame(wire));
  }
  EXPECT_FALSE(ipc::decodeBody(R"({"type":"get_operation","rpc_id":1,"request_id":1,"payload":{"operation_handle":"opaque","operation_id":9}})"));
  auto response = Client::status(1); response.transport_available = false;
  ipc::WireResponse wire; wire.response = response;
  const auto encoded = ipc::encodeFrame(wire); ASSERT_TRUE(encoded);
  EXPECT_EQ(encoded.bytes.find("transport_available"), std::string::npos);
}
TEST(McpToolCatalogTest, FailedOperationQueryIsSuccessfulAndCancellationHidesNumericIds) {
  auto response = Client::status(1); response.result.output = std::monostate{};
  response.result.operation_id = 17;
  OperationSnapshot snapshot; snapshot.operation_id = 17;
  snapshot.state = OperationState::Failed;
  snapshot.submitted_at = snapshot.updated_at = std::chrono::steady_clock::now();
  snapshot.completed_at = snapshot.updated_at;
  ActionResult outcome; outcome.request_id = 9; outcome.operation_id = 17;
  outcome.error_code = ErrorCode::kCaptureFailed; snapshot.outcome = outcome;
  response.control = snapshot;
  auto result = findTool("get_operation")->encode(response, {{"operation_id", "owned"}}, {});
  EXPECT_EQ(result["isError"], false);
  EXPECT_EQ(result["structuredContent"]["operation_id"], "owned");
  EXPECT_EQ(result["structuredContent"]["state"], "failed");
  EXPECT_FALSE(result["structuredContent"]["outcome"].contains("operation_id"));
  EXPECT_FALSE(result["structuredContent"]["outcome"].contains("request_id"));
  response.control = CancellationResult{OperationCancellation{17}, 17, OperationState::Cancelling, true};
  result = findTool("cancel_operation")->encode(response, {{"operation_id", "owned"}}, {});
  EXPECT_EQ(result["isError"], false);
  EXPECT_EQ(result["structuredContent"]["operation_id"], "owned");
  EXPECT_EQ(result["structuredContent"]["cancellation_requested"], true);
  EXPECT_FALSE(result["structuredContent"].contains("target"));
}
