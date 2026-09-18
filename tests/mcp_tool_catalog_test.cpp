#include "mcp_test_client.h"
#include "qingying/automation/automation_contract.h"
#include "automation_wire_codec.h"
#include <set>
#include <algorithm>
using namespace qingying;
using namespace qingying::mcp;
using namespace qingying::mcp::test;
TEST(McpToolCatalogTest, FailureDiagnosticUsesTrustedResponseCorrelation) {
  AutomationResponse response;
  response.connection = {317, 4};
  response.result.request_id = 17;
  response.result.error_code = ErrorCode::kUnknown;
  response.result.failure_stage = "handler";
  const auto output = findTool("crop_center")->encode(
      response, {{"width", 32}, {"height", 24}}, {});
  const auto& diagnostic = output["structuredContent"]["error"]["diagnostic"];
  EXPECT_EQ(diagnostic["correlation_id"], "a317-s4-r17");
  EXPECT_EQ(diagnostic["origin"], "handler");
  EXPECT_EQ(diagnostic["provider_id"], "");
  EXPECT_FALSE(diagnostic.contains("scope_id"));
  EXPECT_EQ(Json::parse(output["content"][0]["text"].get<std::string>()),
            output["structuredContent"]);
}
TEST(McpToolCatalogTest, OnlyImplementedToolsAreRegistered) {
  ASSERT_EQ(tools().size(), 9);
  for (const auto& tool : tools()) {
    const auto descriptor = tool.descriptor({});
    EXPECT_EQ(descriptor["name"], tool.name);
    EXPECT_FALSE(descriptor["inputSchema"]["additionalProperties"].get<bool>());
    Json arguments = tool.kind == ToolKind::CaptureWindow
        ? Json{{"query", "Editor"}}
        : tool.kind == ToolKind::CropCenter
        ? Json{{"width", 32}, {"height", 24}}
        : tool.kind == ToolKind::Copy
            ? Json{{"result_id", "opaque_123"}}
        : tool.kind == ToolKind::Pin
            ? Json{{"result_id", "opaque_123"}}
        : tool.kind == ToolKind::Save
            ? Json{{"result_id", "opaque_123"}, {"path", "C:\\shots"},
                   {"name", "capture.png"}}
            : Json::object();
    if (*tool.argument) arguments[tool.argument] = "opaque_123";
    auto request = tool.decode(arguments, 1, {});
    EXPECT_TRUE(validateAutomationRequest(request).valid);
    arguments["extra"] = 1;
    EXPECT_THROW(tool.decode(arguments, 1, {}), std::invalid_argument);
  }
  EXPECT_NE(findTool("crop_center"), nullptr);
  EXPECT_NE(findTool("save"), nullptr);
  EXPECT_EQ(findTool("capture_region"), nullptr);
  EXPECT_NE(findTool("capture_window"), nullptr);
  EXPECT_NE(findTool("copy"), nullptr);
  EXPECT_NE(findTool("pin"), nullptr);
  EXPECT_EQ(findTool("longshot_select"), nullptr);
}

TEST(McpToolCatalogTest, CaptureWindowSchemaDecodesMatchAndPid) {
  const auto* tool = findTool("capture_window"); ASSERT_NE(tool, nullptr);
  const auto schema = tool->descriptor({})["inputSchema"];
  EXPECT_EQ(schema["required"], Json{"query"});
  auto request = tool->decode({{"query", "中文"}, {"match", "exact"},
                              {"process_id", 42}}, 7, {});
  const auto& window = std::get<CaptureWindowRequest>(
      std::get<ExecuteActionRequest>(request.payload).payload);
  EXPECT_EQ(window.window_query, L"中文");
  EXPECT_EQ(window.match, WindowMatchMode::Exact);
  EXPECT_EQ(window.process_id, 42u);
  EXPECT_THROW(tool->decode({{"query", "x"}, {"match", "regex"}}, 1, {}),
               std::invalid_argument);
  EXPECT_THROW(tool->decode({{"query", "x"}, {"process_id", 0}}, 1, {}),
               std::invalid_argument);
}

TEST(McpToolCatalogTest, CaptureWindowFailureExposesCandidatesWithoutNativeHandles) {
  AutomationResponse response;
  response.result.error_code = ErrorCode::kWindowAmbiguous;
  response.result.message = "window query is ambiguous";
  response.result.output = WindowCandidates{{
      {L"Editor", 42, {1, 2, 3, 4}, "window_opaque"}}, false};
  const auto output = findTool("capture_window")->encode(
      response, {{"query", "Editor"}}, {});
  EXPECT_TRUE(output["isError"]);
  EXPECT_EQ(output["structuredContent"]["error"]["code"],
            "WindowAmbiguous");
  EXPECT_EQ(output["structuredContent"]["candidates"][0]["window_token"],
            "window_opaque");
  EXPECT_EQ(output.dump().find("native_handle"), std::string::npos);
}

TEST(McpToolCatalogTest, SaveRequiresOpaqueResultDirectoryAndPngName) {
  const auto* save = findTool("save"); ASSERT_NE(save, nullptr);
  const auto descriptor = save->descriptor({})["inputSchema"];
  EXPECT_EQ(descriptor["required"], (Json{"result_id", "path", "name"}));
  auto request = save->decode({{"result_id", "owned"}, {"path", "C:\\shots"},
      {"name", "截图.png"}, {"overwrite", true}, {"request_key", "same-save"}}, 7, {});
  const auto& execute = std::get<ExecuteActionRequest>(request.payload);
  const auto& payload = std::get<SaveRequest>(execute.payload);
  EXPECT_EQ(payload.result.kind, ResultSelectionKind::Current);
  EXPECT_EQ(execute.result_handle->value, "owned");
  EXPECT_EQ(payload.path, L"C:\\shots\\截图.png");
  EXPECT_TRUE(payload.overwrite);
  EXPECT_EQ(execute.request_key, "same-save");
  EXPECT_THROW(save->decode({{"path", "C:\\shots"}, {"name", "x.png"}}, 1, {}),
               std::invalid_argument);
  EXPECT_THROW(save->decode({{"result_id", "owned"}, {"path", "C:\\shots"},
      {"name", "x.png"}, {"extra", true}}, 1, {}), std::invalid_argument);
}

TEST(McpToolCatalogTest, CopyRequiresOpaqueResultAndSupportsIdempotencyKey) {
  const auto* copy = findTool("copy"); ASSERT_NE(copy, nullptr);
  const auto schema = copy->descriptor({})["inputSchema"];
  EXPECT_EQ(schema["required"], Json{"result_id"});
  auto request = copy->decode({{"result_id", "owned"},
                               {"request_key", "copy-once"}}, 8, {});
  const auto& execute = std::get<ExecuteActionRequest>(request.payload);
  EXPECT_EQ(std::get<CopyRequest>(execute.payload).result.kind,
            ResultSelectionKind::Current);
  ASSERT_TRUE(execute.result_handle);
  EXPECT_EQ(execute.result_handle->value, "owned");
  EXPECT_EQ(execute.request_key, "copy-once");
  EXPECT_THROW(copy->decode(Json::object(), 1, {}), std::invalid_argument);
  EXPECT_THROW(copy->decode({{"result_id", "owned"}, {"extra", true}}, 1, {}),
               std::invalid_argument);
}

TEST(McpToolCatalogTest, PinRequiresOpaqueResultAndReturnsStablePinId) {
  const auto* pin = findTool("pin"); ASSERT_NE(pin, nullptr);
  EXPECT_EQ(pin->descriptor({})["inputSchema"]["required"], Json{"result_id"});
  auto request = pin->decode({{"result_id", "owned"},
                              {"request_key", "pin-once"}}, 9, {});
  const auto& execute = std::get<ExecuteActionRequest>(request.payload);
  EXPECT_EQ(std::get<PinRequest>(execute.payload).result.kind,
            ResultSelectionKind::Current);
  ASSERT_TRUE(execute.result_handle);
  EXPECT_EQ(execute.result_handle->value, "owned");
  EXPECT_EQ(execute.request_key, "pin-once");

  AutomationResponse response;
  response.result.ok = true;
  response.result.error_code = ErrorCode::kOk;
  response.result.output = PinnedResult{17, 23};
  response.operation_handle = OperationHandle{"pin-operation"};
  const auto output = pin->encode(response, {{"result_id", "owned"}}, {});
  EXPECT_EQ(output["structuredContent"]["result_id"], "owned");
  EXPECT_EQ(output["structuredContent"]["operation_id"], "pin-operation");
  EXPECT_EQ(output["structuredContent"]["pin_id"], 23);
  EXPECT_EQ(output.dump().find("\"result_id\":17"), std::string::npos);
}

TEST(McpToolCatalogTest, CaptureCopyAndSaveOutputsExposeOnlyOpaqueIds) {
  AutomationResponse capture;
  capture.result.ok = true; capture.result.error_code = ErrorCode::kOk;
  capture.result.request_id = 1; capture.result.operation_id = 2;
  capture.result.output = CapturedResult{17, 10, 8, {1, 2, 10, 8}};
  capture.result_handle = ResultHandle{"result-owned"};
  capture.operation_handle = OperationHandle{"capture-operation"};
  auto output = findTool("crop_center")->encode(capture,
      {{"width", 10}, {"height", 8}}, {});
  EXPECT_EQ(output["structuredContent"]["result_id"], "result-owned");
  EXPECT_EQ(output["structuredContent"]["operation_id"], "capture-operation");
  EXPECT_EQ(output.dump().find("\"result_id\":17"), std::string::npos);

  AutomationResponse copied;
  copied.result.ok = true; copied.result.error_code = ErrorCode::kOk;
  copied.result.output = CopiedResult{17};
  copied.operation_handle = OperationHandle{"copy-operation"};
  output = findTool("copy")->encode(copied,
      {{"result_id", "result-owned"}}, {});
  EXPECT_EQ(output["structuredContent"]["result_id"], "result-owned");
  EXPECT_EQ(output["structuredContent"]["operation_id"], "copy-operation");
  EXPECT_EQ(output.dump().find("\"result_id\":17"), std::string::npos);

  AutomationResponse saved;
  saved.result.ok = true; saved.result.error_code = ErrorCode::kOk;
  saved.result.request_id = 3; saved.result.operation_id = 4;
  saved.result.output = SavedResult{17, L"C:\\shots\\x.png"};
  saved.operation_handle = OperationHandle{"save-operation"};
  output = findTool("save")->encode(saved, {{"result_id", "result-owned"}}, {});
  EXPECT_EQ(output["structuredContent"]["result_id"], "result-owned");
  EXPECT_EQ(output["structuredContent"]["operation_id"], "save-operation");
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

TEST(McpToolCatalogTest, RegistryIsTheOnlyDiscoverySource) {
  ASSERT_EQ(tools().size(), actionCatalog().size());
  std::set<std::string> names;
  const auto registered = automationActionDescriptors();
  for (const auto& entry : actionCatalog()) {
    EXPECT_TRUE(names.insert(entry.id).second);
    const auto* tool = findTool(entry.id);
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->kind, entry.kind);
    EXPECT_STREQ(tool->description, entry.description);
    EXPECT_STREQ(tool->argument, entry.handle_argument);
    EXPECT_EQ(entry.cancellable, entry.action && *entry.action != ActionType::Status);
    if (entry.action) {
      EXPECT_NE(entry.validate, nullptr);
      if (*entry.action == ActionType::Status) continue;
      auto found = std::find_if(registered.begin(), registered.end(), [&](const auto& action) {
        return action.type == *entry.action;
      });
      ASSERT_NE(found, registered.end());
      EXPECT_EQ(found->capability, entry.id);
      EXPECT_EQ(found->owns_interaction, entry.owns_interaction);
    }
  }
}

TEST(McpToolCatalogTest, DecodedActionsHaveIdenticalPipeValidationAndRoute) {
  for (const auto& entry : actionCatalog()) {
    if (!entry.action || *entry.action == ActionType::Status) continue;
    Json args;
    switch (entry.kind) {
      case ToolKind::CaptureWindow: args = {{"query", "Editor"}}; break;
      case ToolKind::CropCenter: args = {{"width", 32}, {"height", 24}}; break;
      case ToolKind::Save: args = {{"result_id", "opaque_123"}, {"path", "C:\\shots"}, {"name", "capture.png"}}; break;
      default: args = {{"result_id", "opaque_123"}}; break;
    }
    auto request = findTool(entry.id)->decode(args, 1, {});
    ASSERT_TRUE(validateAutomationRequest(request).valid);
    const auto& execute = std::get<ExecuteActionRequest>(request.payload);
    EXPECT_EQ(actionType(execute.payload), *entry.action);
    ipc::WireRequest wire;
    wire.request = request;
    const auto frame = ipc::encodeFrame(wire);
    ASSERT_TRUE(frame);
  }
}

TEST(McpToolCatalogTest, CommonCaptureParametersAreRejectedByBothTransports) {
  for (const auto& parameters : std::vector<std::pair<std::string, Json>>{
      {"capture_window", {{"query", "x"}, {"match", "regex"}}},
      {"capture_window", {{"query", "x"}, {"process_id", 0}}},
      {"capture_window", {{"query", ""}}},
      {"crop_center", {{"width", 0}, {"height", 24}}},
      {"crop_center", {{"width", 32}, {"height", -1}}}}) {
    EXPECT_THROW(findTool(parameters.first)->decode(parameters.second, 1, {}), std::invalid_argument);
    auto payload = parameters.second;
    payload["action"] = parameters.first;
    Json request{{"type", "execute_action"}, {"rpc_id", 1}, {"request_id", 1}, {"payload", payload}};
    EXPECT_FALSE(ipc::decodeBody(request.dump()));
  }
}

TEST(McpToolCatalogTest, ActionFailureCodeAndMessageMatchPipeResponse) {
  AutomationResponse response;
  response.result.request_id = 1;
  response.result.error_code = ErrorCode::kInvalidArgument;
  response.result.message = "crop dimensions must be positive";
  ipc::WireResponse wire;
  wire.response = response;
  const auto frame = ipc::encodeFrame(wire);
  ASSERT_TRUE(frame);
  const auto decoded = ipc::decodeBody(std::string_view(frame.bytes).substr(4));
  ASSERT_TRUE(decoded);
  const auto& failure = std::get<ipc::WireResponse>(*decoded.message).response.result;
  const auto toolResult = findTool("crop_center")->encode(response, Json::object(), {});
  EXPECT_EQ(toolResult["structuredContent"]["error"]["code"], std::string(errorCodeSymbol(failure.error_code)));
  EXPECT_EQ(toolResult["structuredContent"]["error"]["message"], failure.message);
}
