#include "tool_catalog.h"
#include "qingying/automation/automation_contract.h"
#include "automation_wire_codec.h"
#include "action_catalog_codec.h"
#include <Windows.h>
#include <climits>
#include <filesystem>
namespace qingying::mcp {
namespace {
Json result(Json data, bool failed) {
  return {{"content", Json::array({{{"type", "text"}, {"text", data.dump()}}})},
          {"structuredContent", std::move(data)}, {"isError", failed}};
}
bool opaque(const Json& value, const AutomationLimits& limits) {
  if (!value.is_string()) return false;
  const auto& text = value.get_ref<const std::string&>();
  return !text.empty() && text.size() <= limits.max_opaque_handle_bytes &&
      text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos;
}
std::string utf8(const std::wstring& value, std::size_t limit) {
  if (value.size() > limit || value.size() > INT_MAX)
    throw std::invalid_argument("invalid UTF-16 string");
  if (value.empty()) return {};
  const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr,
      nullptr);
  if (bytes <= 0) throw std::invalid_argument("invalid UTF-16 string");
  std::string output(static_cast<std::size_t>(bytes), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), output.data(), bytes, nullptr,
      nullptr) != bytes)
    throw std::invalid_argument("invalid UTF-16 string");
  return output;
}
}  // namespace
Json toolFailure(int code, const std::string& message) {
  return result({{"error", {{"code", std::string(errorCodeSymbol(code))}, {"message", message}}}}, true);
}
const std::vector<Tool>& tools() {
  static const auto catalog = [] {
    std::vector<Tool> result;
    for (const auto& entry : actionCatalog())
      result.push_back({entry.id, entry.description, entry.handle_argument, entry.kind});
    return result;
  }();
  return catalog;
}
const Tool* findTool(const std::string& name) {
  for (const auto& tool : tools()) if (name == tool.name) return &tool;
  return nullptr;
}
Json Tool::descriptor(const AutomationLimits& limits) const {
  return {{"name", name}, {"description", description},
      {"inputSchema", ipc::actionInputSchema(kind, argument, limits)},
      {"annotations", {{"readOnlyHint", kind == ToolKind::Status || kind == ToolKind::GetOperation},
                        {"openWorldHint", false}}}};
}
AutomationRequest Tool::decode(const Json& arguments, RequestId id, const AutomationLimits& limits) const {
  auto request = ipc::decodeActionArguments(kind, argument, arguments, id, limits);
  const auto validation = validateAutomationRequest(request, limits);
  if (!validation.valid) throw std::invalid_argument(validation.message);
  return request;
}
Json Tool::encode(const AutomationResponse& response, const Json& arguments, const AutomationLimits& limits) const {
  if (!response.result.ok) {
    if (kind == ToolKind::Status && (!response.transport_available || !response.connection.valid()))
      return result({{"reachable", false}, {"app_running", nullptr}, {"automation_enabled", nullptr},
          {"busy", nullptr}, {"limits", nullptr}, {"connection_reason", response.result.message.empty()
              ? std::string(errorCodeSymbol(response.result.error_code)) : response.result.message}}, false);
    if (kind != ToolKind::CaptureWindow ||
        !std::holds_alternative<WindowCandidates>(response.result.output))
      return toolFailure(response.result.error_code, response.result.message);
    const auto& output = std::get<WindowCandidates>(response.result.output);
    if (output.candidates.size() > 64)
      return toolFailure(ErrorCode::kUnknown, "Invalid window candidates");
    Json data{{"candidates", Json::array()}, {"truncated", output.truncated},
              {"error", {{"code", std::string(errorCodeSymbol(response.result.error_code))},
                         {"message", response.result.message}}}};
    for (const auto& candidate : output.candidates) {
      if (candidate.process_id == 0 || !candidate.bounds.valid() ||
          !opaque(Json(candidate.window_token), limits))
        return toolFailure(ErrorCode::kUnknown, "Invalid window candidate");
      data["candidates"].push_back({
          {"title", utf8(candidate.title,
                         limits.max_window_query_utf16_units)},
          {"process_id", candidate.process_id},
          {"bounds", {{"x", candidate.bounds.x}, {"y", candidate.bounds.y},
                      {"width", candidate.bounds.width},
                      {"height", candidate.bounds.height}}},
          {"window_token", candidate.window_token}});
    }
    return result(std::move(data), true);
  }
  if (kind == ToolKind::Copy || kind == ToolKind::Pin) {
    const bool valid_output = kind == ToolKind::Copy
        ? std::holds_alternative<CopiedResult>(response.result.output)
        : std::holds_alternative<PinnedResult>(response.result.output);
    if (!valid_output ||
        !response.operation_handle)
      return toolFailure(ErrorCode::kUnknown, "Expected result action handles");
    Json data{{"result_id", arguments.at("result_id")},
              {"operation_id", response.operation_handle->value}};
    if (kind == ToolKind::Pin)
      data["pin_id"] = std::get<PinnedResult>(response.result.output).pin_id;
    return result(std::move(data), false);
  }
  ipc::WireResponse wire; wire.response = response;
  const auto frame = ipc::encodeFrame(wire, limits);
  if (!frame) return toolFailure(ErrorCode::kUnknown, "Invalid automation response");
  const auto serialized = Json::parse(frame.bytes.substr(4));
  Json data;
  switch (kind) {
    case ToolKind::Status:
      if (!std::holds_alternative<StatusInfo>(response.result.output)) return toolFailure(ErrorCode::kUnknown, "Expected status output");
      data = serialized.at("result").at("output"); data.erase("kind"); break;
    case ToolKind::CropCenter:
    case ToolKind::CaptureWindow:
      if (!std::holds_alternative<CapturedResult>(response.result.output) || !response.result_handle || !response.operation_handle)
        return toolFailure(ErrorCode::kUnknown, "Expected captured result handles");
      data = serialized.at("result").at("output"); data.erase("kind");
      data["result_id"] = response.result_handle->value; data["operation_id"] = response.operation_handle->value; break;
    case ToolKind::Copy:
    case ToolKind::Pin:
      break;
    case ToolKind::Save:
      if (!std::holds_alternative<SavedResult>(response.result.output) || !response.operation_handle)
        return toolFailure(ErrorCode::kUnknown, "Expected saved result handle");
      data = serialized.at("result").at("output"); data.erase("kind");
      data["result_id"] = arguments.at("result_id"); data["operation_id"] = response.operation_handle->value; break;
    case ToolKind::GetOperation:
      if (!std::holds_alternative<OperationSnapshot>(response.control)) return toolFailure(ErrorCode::kUnknown, "Expected operation output");
      data = serialized.at("control").at("operation"); data["operation_id"] = arguments.at(argument);
      if (!data["outcome"].is_null()) { auto& outcome = data["outcome"]; outcome.erase("request_id");
        outcome.erase("operation_id"); outcome.erase("output"); outcome.erase("data"); }
      break;
    case ToolKind::CancelOperation:
      if (!std::holds_alternative<CancellationResult>(response.control)) return toolFailure(ErrorCode::kUnknown, "Expected cancellation output");
      data = serialized.at("control"); data.erase("kind"); data.erase("target"); data["operation_id"] = arguments.at(argument); break;
    case ToolKind::ReleaseResult:
      if (!std::holds_alternative<ReleasedResult>(response.control)) return toolFailure(ErrorCode::kUnknown, "Expected release output");
      data = serialized.at("control"); data.erase("kind"); data["result_id"] = arguments.at(argument); break;
  }
  return result(std::move(data), false);
}
}  // namespace qingying::mcp
