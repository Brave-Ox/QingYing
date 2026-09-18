#include "action_catalog_codec.h"
#include "qingying/automation/automation_contract.h"
#include <Windows.h>
#include <climits>
#include <filesystem>
namespace qingying::ipc {
using Json = nlohmann::json;
namespace {
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
Json handleSchema(std::size_t limit) {
  return {{"type", "string"}, {"minLength", 1}, {"maxLength", limit},
          {"pattern", "^[A-Za-z0-9_-]+$"}};
}
}
Json actionInputSchema(AutomationToolKind kind, const char* argument, const AutomationLimits& limits) {
  Json schema{{"type", "object"}, {"properties", Json::object()},
              {"required", Json::array()}, {"additionalProperties", false}};
  if (kind == AutomationToolKind::CaptureWindow) {
    schema["properties"] = {
      {"query", {{"type", "string"}, {"minLength", 1},
                 {"maxLength", limits.max_window_query_utf16_units}}},
      {"match", {{"type", "string"}, {"enum", {"contains", "exact"}},
                 {"default", "contains"}}},
      {"process_id", {{"type", "integer"}, {"minimum", 1},
                      {"maximum", UINT32_MAX}}}};
    schema["required"] = {"query"};
  } else if (kind == AutomationToolKind::CropCenter) {
    schema["properties"] = {{"width", {{"type", "integer"}, {"minimum", 1}, {"maximum", INT_MAX}}},
                            {"height", {{"type", "integer"}, {"minimum", 1}, {"maximum", INT_MAX}}}};
    schema["required"] = {"width", "height"};
  } else if (kind == AutomationToolKind::Copy || kind == AutomationToolKind::Pin) {
    schema["properties"] = {
      {"result_id", handleSchema(limits.max_opaque_handle_bytes)},
      {"request_key", {{"type", "string"}, {"minLength", 1},
                       {"maxLength", limits.max_request_key_bytes}}}};
    schema["required"] = {"result_id"};
  } else if (kind == AutomationToolKind::Save) {
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
  return schema;
}
AutomationRequest decodeActionArguments(AutomationToolKind kind, const char* argument, const Json& arguments, RequestId id, const AutomationLimits& limits) {
  if (!arguments.is_object()) throw std::invalid_argument("arguments do not match inputSchema");
  if (kind == AutomationToolKind::CaptureWindow) {
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
  if (kind == AutomationToolKind::CropCenter) {
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
  if (kind == AutomationToolKind::Copy || kind == AutomationToolKind::Pin) {
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
    execute.payload = kind == AutomationToolKind::Copy
        ? ActionPayload{CopyRequest{ResultSelection::current()}}
        : ActionPayload{PinRequest{ResultSelection::current()}};
    execute.request_key = std::move(key);
    execute.result_handle = ResultHandle{arguments["result_id"].get<std::string>()};
    AutomationRequest request;
    request.request_id = id;
    request.payload = std::move(execute);
    return request;
  }
  if (kind == AutomationToolKind::Save) {
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
    case AutomationToolKind::Status: break;
    case AutomationToolKind::GetOperation: request.payload = GetOperationRequest{0, OperationHandle{handle}}; break;
    case AutomationToolKind::CancelOperation: request.payload = CancelOperationRequest{OperationCancellation{}, OperationHandle{handle}}; break;
    case AutomationToolKind::ReleaseResult: request.payload = ReleaseResultRequest{0, ResultHandle{handle}}; break;
    default: throw std::invalid_argument("invalid tool decoder");
  }
  return request;
}
}
