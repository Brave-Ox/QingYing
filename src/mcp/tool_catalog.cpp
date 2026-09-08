#include "tool_catalog.h"
#include "automation_wire_codec.h"
namespace qingying::mcp {
namespace {
Json result(Json data, bool failed) {
  return {{"content", Json::array({{{"type", "text"}, {"text", data.dump()}}})},
          {"structuredContent", std::move(data)}, {"isError", failed}};
}
}
Json toolFailure(int code, const std::string& message) {
  return result({{"error", {{"code", std::string(errorCodeSymbol(code))}, {"message", message}}}}, true);
}
const std::array<Tool, 4>& tools() {
  static const std::array<Tool, 4> catalog{{
    {"status", "Query desktop automation availability and limits", "", ToolKind::Status},
    {"get_operation", "Query a connection-owned operation", "operation_id", ToolKind::GetOperation},
    {"cancel_operation", "Request cancellation of a connection-owned operation", "operation_id", ToolKind::CancelOperation},
    {"release_result", "Release a connection-owned result", "result_id", ToolKind::ReleaseResult}
  }};
  return catalog;
}
const Tool* findTool(const std::string& name) {
  for (const auto& tool : tools()) if (name == tool.name) return &tool;
  return nullptr;
}
Json Tool::descriptor(const AutomationLimits& limits) const {
  Json schema{{"type", "object"}, {"properties", Json::object()},
              {"required", Json::array()}, {"additionalProperties", false}};
  if (*argument) {
    schema["properties"][argument] = {{"type", "string"}, {"minLength", 1},
        {"maxLength", limits.max_opaque_handle_bytes}, {"pattern", "^[A-Za-z0-9_-]+$"}};
    schema["required"].push_back(argument);
  }
  return {{"name", name}, {"description", description}, {"inputSchema", schema},
      {"annotations", {{"readOnlyHint", kind == ToolKind::Status || kind == ToolKind::GetOperation},
                        {"openWorldHint", false}}}};
}
AutomationRequest Tool::decode(const Json& arguments, RequestId id, const AutomationLimits& limits) const {
  if (!arguments.is_object() || arguments.size() != (*argument ? 1u : 0u))
    throw std::invalid_argument("arguments do not match inputSchema");
  std::string handle;
  if (*argument) {
    if (!arguments.contains(argument) || !arguments.at(argument).is_string())
      throw std::invalid_argument("expected opaque string handle");
    handle = arguments.at(argument).get<std::string>();
    if (handle.empty() || handle.size() > limits.max_opaque_handle_bytes ||
        handle.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
      throw std::invalid_argument("invalid opaque handle");
  }
  AutomationRequest request;
  request.request_id = id;
  switch (kind) {
    case ToolKind::Status: break;
    case ToolKind::GetOperation: request.payload = GetOperationRequest{0, OperationHandle{handle}}; break;
    case ToolKind::CancelOperation: request.payload = CancelOperationRequest{OperationCancellation{}, OperationHandle{handle}}; break;
    case ToolKind::ReleaseResult: request.payload = ReleaseResultRequest{0, ResultHandle{handle}}; break;
  }
  return request;
}
Json Tool::encode(const AutomationResponse& response, const Json& arguments, const AutomationLimits& limits) const {
  if (!response.result.ok) {
    if (kind == ToolKind::Status && (!response.transport_available || !response.connection.valid()))
      return result({{"reachable", false}, {"app_running", nullptr},
          {"automation_enabled", nullptr}, {"busy", nullptr}, {"limits", nullptr},
          {"connection_reason", response.result.message.empty()
              ? std::string(errorCodeSymbol(response.result.error_code)) : response.result.message}}, false);
    return toolFailure(response.result.error_code, response.result.message);
  }
  ipc::WireResponse wire;
  wire.response = response;
  auto frame = ipc::encodeFrame(wire, limits);
  if (!frame) return toolFailure(ErrorCode::kUnknown, "Invalid automation response");
  const auto serialized = Json::parse(frame.bytes.substr(4));
  Json data;
  switch (kind) {
    case ToolKind::Status:
      if (!std::holds_alternative<StatusInfo>(response.result.output)) return toolFailure(ErrorCode::kUnknown, "Expected status output");
      data = serialized.at("result").at("output"); data.erase("kind"); break;
    case ToolKind::GetOperation:
      if (!std::holds_alternative<OperationSnapshot>(response.control)) return toolFailure(ErrorCode::kUnknown, "Expected operation output");
      data = serialized.at("control").at("operation");
      data["operation_id"] = arguments.at(argument);
      // Never publish numeric private IDs in operation outcomes.
      if (!data["outcome"].is_null()) {
        auto& outcome = data["outcome"];
        outcome.erase("request_id"); outcome.erase("operation_id");
        outcome.erase("output"); outcome.erase("data");
      }
      break;
    case ToolKind::CancelOperation:
      if (!std::holds_alternative<CancellationResult>(response.control)) return toolFailure(ErrorCode::kUnknown, "Expected cancellation output");
      data = serialized.at("control"); data.erase("kind"); data.erase("target");
      data["operation_id"] = arguments.at(argument); break;
    case ToolKind::ReleaseResult:
      if (!std::holds_alternative<ReleasedResult>(response.control)) return toolFailure(ErrorCode::kUnknown, "Expected release output");
      data = serialized.at("control"); data.erase("kind");
      data["result_id"] = arguments.at(argument); break;
  }
  return result(std::move(data), false);
}
}  // namespace qingying::mcp
