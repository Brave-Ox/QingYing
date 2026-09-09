#include "tool_catalog.h"
#include "automation_wire_codec.h"
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
std::wstring wide(const Json& value, std::size_t limit) {
  if (!value.is_string()) throw std::invalid_argument("expected string");
  const auto& text = value.get_ref<const std::string&>();
  if (text.empty() || text.find('\0') != std::string::npos || text.size() > INT_MAX)
    throw std::invalid_argument("invalid string");
  const int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
      static_cast<int>(text.size()), nullptr, 0);
  if (units <= 0 || static_cast<std::size_t>(units) > limit)
    throw std::invalid_argument("invalid UTF-8 string");
  std::wstring output(units, L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
      static_cast<int>(text.size()), output.data(), units) != units)
    throw std::invalid_argument("invalid UTF-8 string");
  return output;
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
Json handleSchema(std::size_t limit) {
  return {{"type", "string"}, {"minLength", 1}, {"maxLength", limit},
          {"pattern", "^[A-Za-z0-9_-]+$"}};
}
}  // namespace
Json toolFailure(int code, const std::string& message) {
  return result({{"error", {{"code", std::string(errorCodeSymbol(code))}, {"message", message}}}}, true);
}
const std::array<Tool, 8>& tools() {
  static const std::array<Tool, 8> catalog{{
    {"status", "Query desktop automation availability and limits", "", ToolKind::Status},
    {"capture_window", "Capture visible desktop pixels for one matching window", "", ToolKind::CaptureWindow},
    {"crop_center", "Capture the physical center of the primary display", "", ToolKind::CropCenter},
    {"copy", "Copy a connection-owned result to the clipboard", "", ToolKind::Copy},
    {"save", "Save a connection-owned result as a PNG file", "", ToolKind::Save},
    {"get_operation", "Query a connection-owned operation", "operation_id", ToolKind::GetOperation},
    {"cancel_operation", "Request cancellation of a connection-owned operation", "operation_id", ToolKind::CancelOperation},
    {"release_result", "Release a connection-owned result", "result_id", ToolKind::ReleaseResult}}};
  return catalog;
}
const Tool* findTool(const std::string& name) {
  for (const auto& tool : tools()) if (name == tool.name) return &tool;
  return nullptr;
}
Json Tool::descriptor(const AutomationLimits& limits) const {
  Json schema{{"type", "object"}, {"properties", Json::object()},
              {"required", Json::array()}, {"additionalProperties", false}};
  if (kind == ToolKind::CaptureWindow) {
    schema["properties"] = {
      {"query", {{"type", "string"}, {"minLength", 1},
                 {"maxLength", limits.max_window_query_utf16_units}}},
      {"match", {{"type", "string"}, {"enum", {"contains", "exact"}},
                 {"default", "contains"}}},
      {"process_id", {{"type", "integer"}, {"minimum", 1},
                      {"maximum", UINT32_MAX}}}};
    schema["required"] = {"query"};
  } else if (kind == ToolKind::CropCenter) {
    schema["properties"] = {{"width", {{"type", "integer"}, {"minimum", 1}, {"maximum", INT_MAX}}},
                            {"height", {{"type", "integer"}, {"minimum", 1}, {"maximum", INT_MAX}}}};
    schema["required"] = {"width", "height"};
  } else if (kind == ToolKind::Copy) {
    schema["properties"] = {
      {"result_id", handleSchema(limits.max_opaque_handle_bytes)},
      {"request_key", {{"type", "string"}, {"minLength", 1},
                       {"maxLength", limits.max_request_key_bytes}}}};
    schema["required"] = {"result_id"};
  } else if (kind == ToolKind::Save) {
    schema["properties"] = {
      {"result_id", handleSchema(limits.max_opaque_handle_bytes)},
      {"path", {{"type", "string"}, {"minLength", 1}, {"maxLength", limits.max_path_utf16_units}}},
      {"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", limits.max_filename_utf16_units}}},
      {"overwrite", {{"type", "boolean"}, {"default", false}}},
      {"request_key", {{"type", "string"}, {"minLength", 1}, {"maxLength", limits.max_request_key_bytes}}}};
    schema["required"] = {"result_id", "path", "name"};
  } else if (*argument) {
    schema["properties"][argument] = handleSchema(limits.max_opaque_handle_bytes);
    schema["required"].push_back(argument);
  }
  return {{"name", name}, {"description", description}, {"inputSchema", schema},
      {"annotations", {{"readOnlyHint", kind == ToolKind::Status || kind == ToolKind::GetOperation},
                        {"openWorldHint", false}}}};
}
AutomationRequest Tool::decode(const Json& arguments, RequestId id, const AutomationLimits& limits) const {
  if (!arguments.is_object()) throw std::invalid_argument("arguments do not match inputSchema");
  if (kind == ToolKind::CaptureWindow) {
    if (arguments.size() < 1 || arguments.size() > 3 ||
        !arguments.contains("query"))
      throw std::invalid_argument("arguments do not match inputSchema");
    for (const auto& item : arguments.items())
      if (item.key() != "query" && item.key() != "match" &&
          item.key() != "process_id")
        throw std::invalid_argument("arguments do not match inputSchema");
    CaptureWindowRequest window;
    window.window_query = wide(arguments.at("query"),
                               limits.max_window_query_utf16_units);
    if (arguments.contains("match")) {
      if (!arguments["match"].is_string())
        throw std::invalid_argument("match must be a string");
      const auto match = arguments["match"].get<std::string>();
      if (match != "contains" && match != "exact")
        throw std::invalid_argument("invalid window match mode");
      window.match = match == "exact" ? WindowMatchMode::Exact
                                      : WindowMatchMode::Contains;
    }
    if (arguments.contains("process_id")) {
      if (!arguments["process_id"].is_number_unsigned() &&
          !arguments["process_id"].is_number_integer())
        throw std::invalid_argument("process_id must be an integer");
      const auto pid = arguments["process_id"].get<std::int64_t>();
      if (pid <= 0 || pid > UINT32_MAX)
        throw std::invalid_argument("invalid process_id");
      window.process_id = static_cast<std::uint32_t>(pid);
    }
    AutomationRequest request;
    request.request_id = id;
    request.payload = ExecuteActionRequest{std::move(window)};
    return request;
  }
  if (kind == ToolKind::CropCenter) {
    if (arguments.size() != 2 || !arguments.contains("width") || !arguments.contains("height") ||
        !arguments["width"].is_number_integer() || !arguments["height"].is_number_integer())
      throw std::invalid_argument("arguments do not match inputSchema");
    const auto width = arguments["width"].get<std::int64_t>();
    const auto height = arguments["height"].get<std::int64_t>();
    if (width <= 0 || width > INT_MAX || height <= 0 || height > INT_MAX)
      throw std::invalid_argument("invalid crop dimensions");
    AutomationRequest request; request.request_id = id;
    request.payload = ExecuteActionRequest{CropCenterRequest{static_cast<int>(width), static_cast<int>(height)}};
    return request;
  }
  if (kind == ToolKind::Copy) {
    if (arguments.empty() || arguments.size() > 2 ||
        !arguments.contains("result_id") ||
        !opaque(arguments.at("result_id"), limits))
      throw std::invalid_argument("arguments do not match inputSchema");
    for (const auto& item : arguments.items())
      if (item.key() != "result_id" && item.key() != "request_key")
        throw std::invalid_argument("arguments do not match inputSchema");
    std::optional<std::string> key;
    if (arguments.contains("request_key")) {
      if (!arguments["request_key"].is_string())
        throw std::invalid_argument("request_key must be string");
      key = arguments["request_key"].get<std::string>();
      if (key->empty() || key->size() > limits.max_request_key_bytes ||
          key->find('\0') != std::string::npos)
        throw std::invalid_argument("invalid request_key");
    }
    ExecuteActionRequest execute;
    execute.payload = CopyRequest{ResultSelection::current()};
    execute.request_key = std::move(key);
    execute.result_handle = ResultHandle{arguments["result_id"].get<std::string>()};
    AutomationRequest request;
    request.request_id = id;
    request.payload = std::move(execute);
    return request;
  }
  if (kind == ToolKind::Save) {
    if (arguments.size() < 3 || arguments.size() > 5 || !arguments.contains("result_id") ||
        !opaque(arguments["result_id"], limits) || !arguments.contains("path") || !arguments.contains("name"))
      throw std::invalid_argument("arguments do not match inputSchema");
    for (const auto& item : arguments.items()) if (item.key() != "result_id" && item.key() != "path" &&
        item.key() != "name" && item.key() != "overwrite" && item.key() != "request_key")
      throw std::invalid_argument("arguments do not match inputSchema");
    auto directory = wide(arguments["path"], limits.max_path_utf16_units);
    auto filename = wide(arguments["name"], limits.max_filename_utf16_units);
    const std::filesystem::path name_path(filename);
    if (name_path.has_root_path() || name_path.has_parent_path() ||
        name_path.filename() != name_path)
      throw std::invalid_argument("name must be one filename");
    bool overwrite = false;
    if (arguments.contains("overwrite")) {
      if (!arguments["overwrite"].is_boolean()) throw std::invalid_argument("overwrite must be boolean");
      overwrite = arguments["overwrite"].get<bool>();
    }
    std::optional<std::string> key;
    if (arguments.contains("request_key")) {
      if (!arguments["request_key"].is_string()) throw std::invalid_argument("request_key must be string");
      key = arguments["request_key"].get<std::string>();
      if (key->empty() || key->size() > limits.max_request_key_bytes || key->find('\0') != std::string::npos)
        throw std::invalid_argument("invalid request_key");
    }
    ExecuteActionRequest execute;
    auto full_path = (std::filesystem::path(directory) / filename).wstring();
    if (full_path.size() > limits.max_path_utf16_units)
      throw std::invalid_argument("combined save path is too long");
    execute.payload = SaveRequest{ResultSelection::current(),
        std::move(full_path), overwrite};
    execute.request_key = std::move(key);
    execute.result_handle = ResultHandle{arguments["result_id"].get<std::string>()};
    AutomationRequest request; request.request_id = id; request.payload = std::move(execute);
    return request;
  }
  if (arguments.size() != (*argument ? 1u : 0u)) throw std::invalid_argument("arguments do not match inputSchema");
  std::string handle;
  if (*argument) {
    if (!arguments.contains(argument) || !opaque(arguments.at(argument), limits))
      throw std::invalid_argument("expected opaque string handle");
    handle = arguments.at(argument).get<std::string>();
  }
  AutomationRequest request; request.request_id = id;
  switch (kind) {
    case ToolKind::Status: break;
    case ToolKind::GetOperation: request.payload = GetOperationRequest{0, OperationHandle{handle}}; break;
    case ToolKind::CancelOperation: request.payload = CancelOperationRequest{OperationCancellation{}, OperationHandle{handle}}; break;
    case ToolKind::ReleaseResult: request.payload = ReleaseResultRequest{0, ResultHandle{handle}}; break;
    default: throw std::invalid_argument("invalid tool decoder");
  }
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
  if (kind == ToolKind::Copy) {
    if (!std::holds_alternative<CopiedResult>(response.result.output) ||
        !response.operation_handle)
      return toolFailure(ErrorCode::kUnknown, "Expected copied result handle");
    return result({{"result_id", arguments.at("result_id")},
                   {"operation_id", response.operation_handle->value}}, false);
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
