#include "automation_wire_codec.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>
#include <streambuf>
#include <ostream>

namespace qingying::ipc {
namespace {
using Json = nlohmann::json;
static_assert(NLOHMANN_JSON_VERSION_MAJOR == 3 && NLOHMANN_JSON_VERSION_MINOR == 11 &&
              NLOHMANN_JSON_VERSION_PATCH == 3, "wire codec requires pinned nlohmann_json 3.11.3");
struct Failure { WireError error; };
void require(bool condition, WireError error = WireError::InvalidMessage) {
  if (!condition) throw Failure{error};
}
bool validLimits(const AutomationLimits& limits) {
  return limits.valid() && limits.max_frame_bytes <= 65536 && limits.max_json_depth <= 16;
}
void checkClock(WireClock::time_point now) {
  require(now >= (WireClock::time_point::min)() + std::chrono::hours{24} &&
          now <= (WireClock::time_point::max)() - std::chrono::hours{24});
}
void fields(const Json& value, std::initializer_list<const char*> required,
            std::initializer_list<const char*> optional = {}) {
  require(value.is_object());
  for (const auto* key : required) require(value.contains(key));
  for (auto it = value.begin(); it != value.end(); ++it) {
    const auto matches = [&](const char* key) { return it.key() == key; };
    require(std::any_of(required.begin(), required.end(), matches) ||
            std::any_of(optional.begin(), optional.end(), matches));
  }
}
std::uint64_t uint(const Json& value, std::uint64_t maximum = UINT64_MAX) {
  require(value.is_number_unsigned() || (value.is_number_integer() && value.get<std::int64_t>() >= 0));
  const auto result = value.get<std::uint64_t>();
  require(result <= maximum);
  return result;
}
std::uint64_t id(const Json& value) {
  const auto result = uint(value);
  require(result != 0);
  return result;
}
int integer(const Json& value) {
  if (value.is_number_unsigned()) return static_cast<int>(uint(value, INT32_MAX));
  require(value.is_number_integer());
  const auto result = value.get<std::int64_t>();
  require(result >= INT32_MIN && result <= INT32_MAX);
  return static_cast<int>(result);
}
bool boolean(const Json& value) { require(value.is_boolean()); return value.get<bool>(); }
const std::string& string(const Json& value, std::size_t max_bytes, bool empty = true) {
  require(value.is_string());
  const auto& result = value.get_ref<const std::string&>();
  require(result.size() <= max_bytes, WireError::ResourceLimit);
  require((empty || !result.empty()) && result.find('\0') == std::string::npos);
  return result;
}
Json textJson(const std::string& value, std::size_t max_bytes, bool empty = true) {
  // Validate before copying a producer-owned string into the JSON DOM.
  require(value.size() <= max_bytes, WireError::ResourceLimit);
  require((empty || !value.empty()) && value.find('\0') == std::string::npos);
  return value;
}
std::wstring wide(const Json& value, std::size_t max_units) {
  const auto& bytes = string(value, max_units * 4);
  if (bytes.empty()) return {};
  require(bytes.size() <= INT32_MAX, WireError::ResourceLimit);
  const auto length = static_cast<int>(bytes.size());
  const int units = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), length, nullptr, 0);
  require(units > 0);
  require(static_cast<std::size_t>(units) <= max_units, WireError::ResourceLimit);
  std::wstring result(static_cast<std::size_t>(units), L'\0');
  require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), length, result.data(), units) == units);
  return result;
}
std::string utf8(const std::wstring& value, std::size_t max_units) {
  require(value.size() <= max_units, WireError::ResourceLimit);
  require(value.find(L'\0') == std::wstring::npos);
  if (value.empty()) return {};
  require(value.size() <= INT32_MAX, WireError::ResourceLimit);
  const auto length = static_cast<int>(value.size());
  const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, nullptr, 0, nullptr, nullptr);
  require(count > 0);
  std::string result(static_cast<std::size_t>(count), '\0');
  require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), length, result.data(), count, nullptr, nullptr) == count);
  return result;
}
void path(const std::wstring& value, const AutomationLimits& limits) {
  require(!value.empty());
  const auto separator = value.find_last_of(L"/\\");
  const auto count = separator == std::wstring::npos ? value.size() : value.size() - separator - 1;
  require(count > 0);
  require(count <= limits.max_filename_utf16_units, WireError::ResourceLimit);
}

// SAX preflight aborts before constructing a DOM. The library owns all JSON
// grammar/UTF-8/escape handling; no permissive handwritten parser is used.
struct Preflight final : nlohmann::json_sax<Json> {
  explicit Preflight(const AutomationLimits& value) : limits(value) {}
  const AutomationLimits& limits;
  WireError error{WireError::InvalidJson};
  std::vector<std::set<std::string>> keys;
  std::vector<std::string> current_keys;
  bool null() override { return true; }
  bool boolean(bool) override { return true; }
  bool number_integer(number_integer_t) override { return true; }
  bool number_unsigned(number_unsigned_t) override { return true; }
  bool number_float(number_float_t, const string_t&) override {
    error = WireError::InvalidMessage; return false;
  }
  bool string(string_t& text) override {
    if (current_keys.empty()) return true;
    const auto& name = current_keys.back();
    std::size_t max_units = 0;
    if (name == "query" || name == "title") max_units = limits.max_window_query_utf16_units;
    if (name == "path" || name == "absolute_path") max_units = limits.max_path_utf16_units;
    if (max_units) {
      std::size_t units = 0, filename_units = 0;
      // SAX has already validated UTF-8. Count UTF-16 units without allocating
      // a wide string/DOM; continuation bytes add no units.
      for (unsigned char byte : text) {
        if (byte < 0x80 || byte >= 0xC0) {
          const auto added = byte >= 0xF0 ? 2u : 1u;
          units += added;
          filename_units += added;
          if (byte == '/' || byte == '\\') filename_units = 0;
        }
      }
      if (units > max_units || ((name == "path" || name == "absolute_path") &&
          filename_units > limits.max_filename_utf16_units)) {
        error = WireError::ResourceLimit; return false;
      }
    }
    if ((name == "request_key" && text.size() > limits.max_request_key_bytes) ||
        ((name == "result_handle" || name == "operation_handle" || name == "window_token") &&
         text.size() > limits.max_opaque_handle_bytes)) {
      error = WireError::ResourceLimit; return false;
    }
    return true;
  }
  bool binary(binary_t&) override { return false; }
  bool start_object(std::size_t) override { return start(); }
  bool start_array(std::size_t) override { return start(); }
  bool start() {
    if (keys.size() >= limits.max_json_depth) { error = WireError::DepthLimit; return false; }
    keys.emplace_back(); current_keys.emplace_back(); return true;
  }
  bool key(string_t& name) override {
    if (!keys.back().insert(name).second) { error = WireError::DuplicateKey; return false; }
    current_keys.back() = name;
    return true;
  }
  bool end_object() override { keys.pop_back(); current_keys.pop_back(); return true; }
  bool end_array() override { keys.pop_back(); current_keys.pop_back(); return true; }
  bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }
};
Json parse(std::string_view body, const AutomationLimits& limits) {
  require(!body.empty() && body.size() <= limits.max_frame_bytes, WireError::InvalidFrameLength);
  // A UTF-8 BOM is not part of this wire profile (the library otherwise skips it).
  require(body.substr(0, 3) != std::string_view("\xEF\xBB\xBF", 3), WireError::InvalidJson);
  Preflight sax{limits};
  const bool valid = Json::sax_parse(body.begin(), body.end(), &sax);
  require(valid, sax.error);
  return Json::parse(body.begin(), body.end());
}
Json limitsJson(const AutomationLimits& value) {
  require(validLimits(value));
  Json result = Json::object();
  result["max_connections"] = value.max_connections;
  result["max_results_per_scope"] = value.max_results_per_scope;
  result["max_desktop_operations"] = value.max_desktop_operations;
  result["max_queued_requests_per_connection"] = value.max_queued_requests_per_connection;
  result["max_queued_requests_global"] = value.max_queued_requests_global;
  result["max_completed_operations_per_connection"] = value.max_completed_operations_per_connection;
  result["max_agent_pins"] = value.max_agent_pins;
  result["max_json_depth"] = value.max_json_depth;
  result["max_control_requests_per_connection"] = value.max_control_requests_per_connection;
  result["max_control_requests_global"] = value.max_control_requests_global;
  result["max_output_frames_per_connection"] = value.max_output_frames_per_connection;
  result["max_export_workers"] = value.max_export_workers;
  result["max_queued_exports"] = value.max_queued_exports;
  result["max_window_query_utf16_units"] = value.max_window_query_utf16_units;
  result["max_filename_utf16_units"] = value.max_filename_utf16_units;
  result["max_path_utf16_units"] = value.max_path_utf16_units;
  result["max_request_key_bytes"] = value.max_request_key_bytes;
  result["max_opaque_handle_bytes"] = value.max_opaque_handle_bytes;
  result["max_tombstones_per_connection"] = value.max_tombstones_per_connection;
  result["max_capture_pixels"] = value.max_capture_pixels;
  result["max_result_bytes"] = value.max_result_bytes;
  result["max_retained_result_bytes"] = value.max_retained_result_bytes;
  result["max_agent_pin_bytes"] = value.max_agent_pin_bytes;
  result["max_frame_bytes"] = value.max_frame_bytes;
  result["max_output_bytes_per_connection"] = value.max_output_bytes_per_connection;
  result["result_ttl_ms"] = value.result_ttl.count();
  result["completed_operation_ttl_ms"] = value.completed_operation_ttl.count();
  result["default_longshot_timeout_ms"] = value.default_longshot_timeout.count();
  result["default_request_timeout_ms"] = value.default_request_timeout.count();
  result["max_request_timeout_ms"] = value.max_request_timeout.count();
  result["tombstone_ttl_ms"] = value.tombstone_ttl.count();
  return result;
}
AutomationLimits readLimits(const Json& value) {
  fields(value, {
      "max_connections", "max_results_per_scope", "max_desktop_operations",
      "max_queued_requests_per_connection", "max_queued_requests_global", "max_completed_operations_per_connection",
      "max_agent_pins", "max_json_depth", "max_control_requests_per_connection",
      "max_control_requests_global", "max_output_frames_per_connection", "max_export_workers",
      "max_queued_exports", "max_window_query_utf16_units", "max_filename_utf16_units",
      "max_path_utf16_units", "max_request_key_bytes", "max_opaque_handle_bytes",
      "max_tombstones_per_connection", "max_capture_pixels", "max_result_bytes",
      "max_retained_result_bytes", "max_agent_pin_bytes", "max_frame_bytes",
      "max_output_bytes_per_connection", "result_ttl_ms", "completed_operation_ttl_ms",
      "default_longshot_timeout_ms", "default_request_timeout_ms", "max_request_timeout_ms",
      "tombstone_ttl_ms"});
  AutomationLimits result;
  result.max_connections = static_cast<std::uint32_t>(uint(value.at("max_connections"), UINT32_MAX));
  result.max_results_per_scope = static_cast<std::uint32_t>(uint(value.at("max_results_per_scope"), UINT32_MAX));
  result.max_desktop_operations = static_cast<std::uint32_t>(uint(value.at("max_desktop_operations"), UINT32_MAX));
  result.max_queued_requests_per_connection = static_cast<std::uint32_t>(uint(value.at("max_queued_requests_per_connection"), UINT32_MAX));
  result.max_queued_requests_global = static_cast<std::uint32_t>(uint(value.at("max_queued_requests_global"), UINT32_MAX));
  result.max_completed_operations_per_connection = static_cast<std::uint32_t>(uint(value.at("max_completed_operations_per_connection"), UINT32_MAX));
  result.max_agent_pins = static_cast<std::uint32_t>(uint(value.at("max_agent_pins"), UINT32_MAX));
  result.max_json_depth = static_cast<std::uint32_t>(uint(value.at("max_json_depth"), UINT32_MAX));
  result.max_control_requests_per_connection = static_cast<std::uint32_t>(uint(value.at("max_control_requests_per_connection"), UINT32_MAX));
  result.max_control_requests_global = static_cast<std::uint32_t>(uint(value.at("max_control_requests_global"), UINT32_MAX));
  result.max_output_frames_per_connection = static_cast<std::uint32_t>(uint(value.at("max_output_frames_per_connection"), UINT32_MAX));
  result.max_export_workers = static_cast<std::uint32_t>(uint(value.at("max_export_workers"), UINT32_MAX));
  result.max_queued_exports = static_cast<std::uint32_t>(uint(value.at("max_queued_exports"), UINT32_MAX));
  result.max_window_query_utf16_units = static_cast<std::uint32_t>(uint(value.at("max_window_query_utf16_units"), UINT32_MAX));
  result.max_filename_utf16_units = static_cast<std::uint32_t>(uint(value.at("max_filename_utf16_units"), UINT32_MAX));
  result.max_path_utf16_units = static_cast<std::uint32_t>(uint(value.at("max_path_utf16_units"), UINT32_MAX));
  result.max_request_key_bytes = static_cast<std::uint32_t>(uint(value.at("max_request_key_bytes"), UINT32_MAX));
  result.max_opaque_handle_bytes = static_cast<std::uint32_t>(uint(value.at("max_opaque_handle_bytes"), UINT32_MAX));
  result.max_tombstones_per_connection = static_cast<std::uint32_t>(uint(value.at("max_tombstones_per_connection"), UINT32_MAX));
  result.max_capture_pixels = uint(value.at("max_capture_pixels"));
  result.max_result_bytes = uint(value.at("max_result_bytes"));
  result.max_retained_result_bytes = uint(value.at("max_retained_result_bytes"));
  result.max_agent_pin_bytes = uint(value.at("max_agent_pin_bytes"));
  result.max_frame_bytes = uint(value.at("max_frame_bytes"));
  result.max_output_bytes_per_connection = uint(value.at("max_output_bytes_per_connection"));
  result.result_ttl = std::chrono::milliseconds{uint(value.at("result_ttl_ms"), 86400000)};
  result.completed_operation_ttl = std::chrono::milliseconds{uint(value.at("completed_operation_ttl_ms"), 86400000)};
  result.default_longshot_timeout = std::chrono::milliseconds{uint(value.at("default_longshot_timeout_ms"), 86400000)};
  result.default_request_timeout = std::chrono::milliseconds{uint(value.at("default_request_timeout_ms"), 86400000)};
  result.max_request_timeout = std::chrono::milliseconds{uint(value.at("max_request_timeout_ms"), 86400000)};
  result.tombstone_ttl = std::chrono::milliseconds{uint(value.at("tombstone_ttl_ms"), 86400000)};
  require(validLimits(result));
  return result;
}
const std::set<std::string> capabilities{
    "status", "capture_region", "capture_window", "crop_center", "save", "copy",
    "pin", "longshot_select", "get_operation", "cancel_operation", "release_result"};
Json capabilitiesJson(const std::vector<std::string>& names) {
  require(names.size() <= capabilities.size());
  for (const auto& name : names) require(capabilities.count(name) != 0);
  return names;
}
std::vector<std::string> readCapabilities(const Json& value) {
  require(value.is_array() && value.size() <= capabilities.size());
  std::vector<std::string> result;
  std::set<std::string> unique;
  for (const auto& item : value) {
    const auto& name = string(item, 64, false);
    require(capabilities.count(name) && unique.insert(name).second);
    result.push_back(name);
  }
  return result;
}
Json rpcJson(const RpcId& value, const AutomationLimits& limits) {
  return std::visit([&](const auto& id_value) -> Json {
    using T = std::decay_t<decltype(id_value)>;
    if constexpr (std::is_same_v<T, std::string>) {
      require(id_value.size() <= limits.max_request_key_bytes, WireError::ResourceLimit);
      require(id_value.find('\0') == std::string::npos);
    }
    return id_value;
  }, value);
}
RpcId readRpc(const Json& value, const AutomationLimits& limits) {
  if (value.is_string()) return string(value, limits.max_request_key_bytes);
  if (value.is_number_unsigned()) return value.get<std::uint64_t>();
  require(value.is_number_integer());
  return value.get<std::int64_t>();
}
Json rectJson(const ScreenPhysicalRect& value) {
  return {{"x", value.x}, {"y", value.y}, {"width", value.width}, {"height", value.height}};
}
ScreenPhysicalRect readRect(const Json& value) {
  fields(value, {"x", "y", "width", "height"});
  ScreenPhysicalRect result{integer(value.at("x")), integer(value.at("y")),
      integer(value.at("width")), integer(value.at("height"))};
  require(result.width >= 0 && result.height >= 0 &&
          static_cast<std::int64_t>(result.x) + result.width <= INT32_MAX &&
          static_cast<std::int64_t>(result.y) + result.height <= INT32_MAX);
  return result;
}
Json targetJson(const CancellationTarget& target) {
  return std::visit([](const auto& value) -> Json {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, OperationCancellation>) return {{"operation_id", value.operation_id}};
    else return {{"request_id", value.request_id}};
  }, target);
}
CancellationTarget readTarget(const Json& value) {
  if (value.contains("operation_id")) {
    fields(value, {"operation_id"}); return OperationCancellation{id(value.at("operation_id"))};
  }
  fields(value, {"request_id"}); return RequestCancellation{id(value.at("request_id"))};
}

const std::vector<std::pair<OperationState, const char*>> states{
    {OperationState::Queued, "queued"}, {OperationState::AwaitingUser, "awaiting_user"},
    {OperationState::Running, "running"}, {OperationState::Paused, "paused"},
    {OperationState::Finalizing, "finalizing"}, {OperationState::Cancelling, "cancelling"},
    {OperationState::Succeeded, "succeeded"}, {OperationState::Failed, "failed"},
    {OperationState::Cancelled, "cancelled"}, {OperationState::TimedOut, "timed_out"}};
const std::vector<std::pair<AbortReason, const char*>> aborts{
    {AbortReason::None, "none"}, {AbortReason::UserCancel, "user_cancel"},
    {AbortReason::ClientCancel, "client_cancel"}, {AbortReason::Deadline, "deadline"},
    {AbortReason::Disconnect, "disconnect"}, {AbortReason::Shutdown, "shutdown"}};
const std::vector<std::pair<ResultAvailability, const char*>> availability{
    {ResultAvailability::None, "none"}, {ResultAvailability::Available, "available"},
    {ResultAvailability::Released, "released"}, {ResultAvailability::Expired, "expired"}};
template <typename T> const char* enumName(T value, const std::vector<std::pair<T, const char*>>& names) {
  for (const auto& pair : names) if (pair.first == value) return pair.second;
  throw Failure{WireError::InvalidMessage};
}
template <typename T> T readEnum(const Json& value, const std::vector<std::pair<T, const char*>>& names) {
  const auto& name = string(value, 32);
  for (const auto& pair : names) if (name == pair.second) return pair.first;
  throw Failure{WireError::InvalidMessage};
}
Json actionJson(const ExecuteActionRequest& execute, const AutomationLimits& limits) {
  auto result = std::visit([&](const auto& value) -> Json {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, StatusRequest>) return {{"action", "status"}};
    else if constexpr (std::is_same_v<T, CaptureRegionRequest>)
      return {{"action", "capture_region"}, {"region", rectJson(value.region)}};
    else if constexpr (std::is_same_v<T, CaptureWindowRequest>)
      return {{"action", "capture_window"}, {"query", utf8(value.window_query, limits.max_window_query_utf16_units)}};
    else if constexpr (std::is_same_v<T, CropCenterRequest>)
      return {{"action", "crop_center"}, {"width", value.width}, {"height", value.height}};
    else {
      require(value.result.kind == ResultSelectionKind::Explicit);
      if constexpr (std::is_same_v<T, SaveRequest>) {
        path(value.path, limits);
        return {{"action", "save"}, {"result_id", value.result.result_id},
                {"path", utf8(value.path, limits.max_path_utf16_units)}};
      } else return {{"action", std::is_same_v<T, CopyRequest> ? "copy" : "pin"},
                    {"result_id", value.result.result_id}};
    }
  }, execute.payload);
  if (execute.request_key) result["request_key"] = textJson(*execute.request_key, limits.max_request_key_bytes, false);
  return result;
}
ExecuteActionRequest readAction(const Json& value, const AutomationLimits& limits) {
  require(value.is_object() && value.contains("action"));
  const auto& name = string(value.at("action"), 32);
  ExecuteActionRequest result;
  if (name == "status") { fields(value, {"action"}); result.payload = StatusRequest{}; }
  else if (name == "capture_region") {
    fields(value, {"action", "region"}); result.payload = CaptureRegionRequest{readRect(value.at("region"))};
  } else if (name == "capture_window") {
    fields(value, {"action", "query"});
    result.payload = CaptureWindowRequest{wide(value.at("query"), limits.max_window_query_utf16_units)};
  } else if (name == "crop_center") {
    fields(value, {"action", "width", "height"});
    result.payload = CropCenterRequest{integer(value.at("width")), integer(value.at("height"))};
  } else if (name == "copy" || name == "pin" || name == "save") {
    const bool save = name == "save";
    if (save) fields(value, {"action", "result_id", "path"}, {"request_key"});
    else fields(value, {"action", "result_id"}, {"request_key"});
    const auto selection = ResultSelection::specific(id(value.at("result_id")));
    if (save) {
      auto full_path = wide(value.at("path"), limits.max_path_utf16_units);
      path(full_path, limits);
      result.payload = SaveRequest{selection, std::move(full_path)};
    } else if (name == "copy") result.payload = CopyRequest{selection};
    else result.payload = PinRequest{selection};
    if (value.contains("request_key")) result.request_key = string(value.at("request_key"), limits.max_request_key_bytes, false);
  } else throw Failure{WireError::InvalidMessage};
  return result;
}
Json requestJson(const WireRequest& value, const AutomationLimits& limits) {
  require(validateAutomationRequest(value.request, limits).valid);
  Json result{{"rpc_id", rpcJson(value.rpc_id, limits)}, {"request_id", value.request.request_id}};
  if (value.request.timeout) result["timeout_ms"] = value.request.timeout->count();
  std::visit([&](const auto& payload) {
    using T = std::decay_t<decltype(payload)>;
    if constexpr (std::is_same_v<T, ExecuteActionRequest>) {
      result["type"] = "execute_action"; result["payload"] = actionJson(payload, limits);
    } else if constexpr (std::is_same_v<T, BeginLongShotRequest>) {
      result["type"] = "begin_longshot"; result["payload"] = Json::object();
    } else if constexpr (std::is_same_v<T, GetOperationRequest>) {
      result["type"] = "get_operation"; result["payload"] = {{"operation_id", payload.operation_id}};
    } else if constexpr (std::is_same_v<T, CancelOperationRequest>) {
      result["type"] = "cancel_operation"; result["payload"] = targetJson(payload.target);
    } else {
      result["type"] = "release_result"; result["payload"] = {{"result_id", payload.result_id}};
    }
  }, value.request.payload);
  return result;
}
WireRequest readRequest(const Json& value, const std::string& type, const AutomationLimits& limits) {
  fields(value, {"type", "rpc_id", "request_id", "payload"}, {"timeout_ms"});
  WireRequest result;
  result.rpc_id = readRpc(value.at("rpc_id"), limits);
  result.request.request_id = id(value.at("request_id"));
  if (value.contains("timeout_ms")) result.request.timeout = std::chrono::milliseconds{
      uint(value.at("timeout_ms"), static_cast<std::uint64_t>(limits.max_request_timeout.count()))};
  const auto& payload = value.at("payload");
  if (type == "execute_action") result.request.payload = readAction(payload, limits);
  else if (type == "begin_longshot") { fields(payload, {}); result.request.payload = BeginLongShotRequest{}; }
  else if (type == "get_operation") {
    fields(payload, {"operation_id"}); result.request.payload = GetOperationRequest{id(payload.at("operation_id"))};
  } else if (type == "cancel_operation") result.request.payload = CancelOperationRequest{readTarget(payload)};
  else if (type == "release_result") {
    fields(payload, {"result_id"}); result.request.payload = ReleaseResultRequest{id(payload.at("result_id"))};
  } else throw Failure{WireError::InvalidMessage};
  require(validateAutomationRequest(result.request, limits).valid);
  return result;
}

Json optionalBool(const std::optional<bool>& value) { return value ? Json(*value) : Json(nullptr); }
std::optional<bool> readOptionalBool(const Json& value) {
  return value.is_null() ? std::nullopt : std::optional<bool>{boolean(value)};
}
Json statusJson(const StatusInfo& value) {
  Json result{{"kind", "status"}, {"reachable", value.reachable},
      {"app_running", optionalBool(value.app_running)}, {"automation_enabled", optionalBool(value.automation_enabled)},
      {"busy", optionalBool(value.busy)}, {"busy_reason", textJson(value.busy_reason, 1024)},
      {"build_version", textJson(value.build_version, 128)},
      {"connection_reason", textJson(value.connection_reason, 1024)}};
  require(value.capabilities.size() <= capabilities.size());
  result["capabilities"] = capabilitiesJson(value.capabilities);
  (void)readCapabilities(result["capabilities"]);
  if (value.limits) result["limits"] = limitsJson(*value.limits);
  if (value.resources) {
    const auto& r = *value.resources;
    result["resources"] = {{"result_bytes", r.result_bytes}, {"reserved_result_bytes", r.reserved_result_bytes},
        {"agent_pin_count", r.agent_pin_count}, {"agent_pin_bytes", r.agent_pin_bytes}};
  }
  if (value.queues) {
    const auto& q = *value.queues;
    result["queues"] = {{"queued", q.queued}, {"running", q.running}, {"ordinary", q.ordinary}, {"control", q.control}};
  }
  return result;
}
StatusInfo readStatus(const Json& value) {
  fields(value, {"kind", "reachable", "app_running", "automation_enabled", "busy", "busy_reason",
      "build_version", "connection_reason", "capabilities"}, {"limits", "resources", "queues"});
  StatusInfo result;
  result.reachable = boolean(value.at("reachable"));
  result.app_running = readOptionalBool(value.at("app_running"));
  result.automation_enabled = readOptionalBool(value.at("automation_enabled"));
  result.busy = readOptionalBool(value.at("busy"));
  result.busy_reason = string(value.at("busy_reason"), 1024);
  result.build_version = string(value.at("build_version"), 128);
  result.connection_reason = string(value.at("connection_reason"), 1024);
  result.capabilities = readCapabilities(value.at("capabilities"));
  if (value.contains("limits")) result.limits = readLimits(value.at("limits"));
  if (value.contains("resources")) {
    const auto& r = value.at("resources");
    fields(r, {"result_bytes", "reserved_result_bytes", "agent_pin_count", "agent_pin_bytes"});
    result.resources = ResourceUsage{uint(r.at("result_bytes")),
        static_cast<std::uint32_t>(uint(r.at("agent_pin_count"), UINT32_MAX)),
        uint(r.at("agent_pin_bytes")), uint(r.at("reserved_result_bytes"))};
  }
  if (value.contains("queues")) {
    const auto& q = value.at("queues"); fields(q, {"queued", "running", "ordinary", "control"});
    result.queues = QueueUsage{static_cast<std::uint32_t>(uint(q.at("queued"), UINT32_MAX)),
        static_cast<std::uint32_t>(uint(q.at("running"), UINT32_MAX)),
        static_cast<std::uint32_t>(uint(q.at("ordinary"), UINT32_MAX)),
        static_cast<std::uint32_t>(uint(q.at("control"), UINT32_MAX))};
    require(static_cast<std::uint64_t>(result.queues->queued) + result.queues->running ==
            static_cast<std::uint64_t>(result.queues->ordinary) + result.queues->control);
  }
  return result;
}

std::int64_t remaining(WireClock::time_point deadline, WireClock::time_point now) {
  if (deadline <= now) return 0;
  // Wire time values are bounded relative milliseconds, never native ticks.
  require(deadline <= now + std::chrono::hours{24});
  return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
}
std::int64_t age(WireClock::time_point time, WireClock::time_point now) {
  require(time <= now && time >= now - std::chrono::hours{24});
  return std::chrono::duration_cast<std::chrono::milliseconds>(now - time).count();
}
Json outputJson(const ActionOutput& output, const AutomationLimits& limits, WireClock::time_point now) {
  return std::visit([&](const auto& value) -> Json {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, std::monostate>) return {{"kind", "none"}};
    else if constexpr (std::is_same_v<T, StatusInfo>) return statusJson(value);
    else if constexpr (std::is_same_v<T, CapturedResult>) {
      require(value.capture_mode == CaptureMode::VisibleScreen);
      Json result{{"kind", "captured"}, {"result_id", value.result_id}, {"width", value.width},
          {"height", value.height}, {"bounds", rectJson(value.bounds)}, {"capture_mode", "visible_screen"}};
      result["expires_in_ms"] = value.expires_at ? Json(remaining(*value.expires_at, now)) : Json(nullptr);
      return result;
    } else if constexpr (std::is_same_v<T, SavedResult>) {
      require(value.format == ImageFormat::Png);
      path(value.absolute_path, limits);
      return {{"kind", "saved"}, {"result_id", value.result_id}, {"format", "png"},
          {"absolute_path", utf8(value.absolute_path, limits.max_path_utf16_units)}};
    } else if constexpr (std::is_same_v<T, CopiedResult>) return {{"kind", "copied"}, {"result_id", value.result_id}};
    else if constexpr (std::is_same_v<T, PinnedResult>) return {{"kind", "pinned"}, {"result_id", value.result_id}, {"pin_id", value.pin_id}};
    else {
      // No dedicated candidate count exists yet; cap control metadata at 64.
      require(value.candidates.size() <= 64, WireError::ResourceLimit);
      Json result{{"kind", "window_candidates"}, {"truncated", value.truncated}, {"candidates", Json::array()}};
      for (const auto& candidate : value.candidates) result["candidates"].push_back({
          {"title", utf8(candidate.title, limits.max_window_query_utf16_units)},
          {"process_id", candidate.process_id}, {"bounds", rectJson(candidate.bounds)},
          {"window_token", textJson(candidate.window_token, limits.max_opaque_handle_bytes, false)}});
      return result;
    }
  }, output);
}
ActionOutput readOutput(const Json& value, const AutomationLimits& limits, WireClock::time_point now) {
  require(value.is_object() && value.contains("kind"));
  const auto& kind = string(value.at("kind"), 32);
  if (kind == "none") { fields(value, {"kind"}); return std::monostate{}; }
  if (kind == "status") return readStatus(value);
  if (kind == "captured") {
    fields(value, {"kind", "result_id", "width", "height", "bounds", "capture_mode", "expires_in_ms"});
    require(value.at("capture_mode") == "visible_screen");
    CapturedResult result{id(value.at("result_id")), integer(value.at("width")), integer(value.at("height")), readRect(value.at("bounds"))};
    require(result.width > 0 && result.height > 0);
    if (!value.at("expires_in_ms").is_null()) result.expires_at = now + std::chrono::milliseconds{
        uint(value.at("expires_in_ms"), 86400000)};
    return result;
  }
  if (kind == "saved") {
    fields(value, {"kind", "result_id", "absolute_path", "format"});
    require(value.at("format") == "png");
    auto full_path = wide(value.at("absolute_path"), limits.max_path_utf16_units);
    path(full_path, limits);
    return SavedResult{id(value.at("result_id")), std::move(full_path), ImageFormat::Png};
  }
  if (kind == "copied") {
    fields(value, {"kind", "result_id"}); return CopiedResult{id(value.at("result_id"))};
  }
  if (kind == "pinned") {
    fields(value, {"kind", "result_id", "pin_id"}); return PinnedResult{id(value.at("result_id")), id(value.at("pin_id"))};
  }
  if (kind == "window_candidates") {
    fields(value, {"kind", "candidates", "truncated"});
    const auto& candidates = value.at("candidates");
    require(candidates.is_array() && candidates.size() <= 64, WireError::ResourceLimit);
    WindowCandidates result;
    result.truncated = boolean(value.at("truncated"));
    for (const auto& candidate : candidates) {
      fields(candidate, {"title", "process_id", "bounds", "window_token"});
      auto bounds = readRect(candidate.at("bounds"));
      require(bounds.valid());
      result.candidates.push_back({wide(candidate.at("title"), limits.max_window_query_utf16_units),
          static_cast<std::uint32_t>(uint(candidate.at("process_id"), UINT32_MAX)), bounds,
          string(candidate.at("window_token"), limits.max_opaque_handle_bytes, false)});
    }
    return result;
  }
  throw Failure{WireError::InvalidMessage};
}
Json resultJson(const ActionResult& value, const AutomationLimits& limits, WireClock::time_point now) {
  return {{"request_id", value.request_id}, {"operation_id", value.operation_id},
      {"ok", value.ok}, {"error_code", value.error_code},
      {"message", textJson(value.message, limits.max_frame_bytes)},
      {"data", textJson(value.data, limits.max_frame_bytes)},
      {"failure_stage", textJson(value.failure_stage, 1024)}, {"failure_frame", value.failure_frame},
      {"output", outputJson(value.output, limits, now)}};
}
ActionResult readResult(const Json& value, const AutomationLimits& limits, WireClock::time_point now) {
  fields(value, {"request_id", "operation_id", "ok", "error_code", "message", "data", "failure_stage", "failure_frame", "output"});
  ActionResult result;
  result.request_id = id(value.at("request_id"));
  result.operation_id = uint(value.at("operation_id"));
  result.ok = boolean(value.at("ok"));
  result.error_code = integer(value.at("error_code"));
  require(result.error_code >= 0 && result.ok == (result.error_code == ErrorCode::kOk));
  result.message = string(value.at("message"), limits.max_frame_bytes);
  result.data = string(value.at("data"), limits.max_frame_bytes);
  result.failure_stage = string(value.at("failure_stage"), 1024);
  result.failure_frame = static_cast<int>(uint(value.at("failure_frame"), INT32_MAX));
  result.output = readOutput(value.at("output"), limits, now);
  return result;
}
Json snapshotJson(const OperationSnapshot& value, const AutomationLimits& limits, WireClock::time_point now) {
  Json result{{"operation_id", value.operation_id}, {"state", enumName(value.state, states)},
      {"abort_reason", enumName(value.abort_reason, aborts)}, {"committed", value.committed},
      {"result_availability", enumName(value.result_availability, availability)},
      {"submitted_ago_ms", age(value.submitted_at, now)}, {"updated_ago_ms", age(value.updated_at, now)},
      {"progress", {{"stage", textJson(value.progress.stage, 1024)}, {"frames", value.progress.frames},
          {"width", value.progress.width}, {"height", value.progress.height},
          {"stop_reason", textJson(value.progress.stop_reason, 1024)}}}};
  result["completed_ago_ms"] = value.completed_at ? Json(age(*value.completed_at, now)) : Json(nullptr);
  result["outcome"] = value.outcome ? resultJson(*value.outcome, limits, now) : Json(nullptr);
  return result;
}
OperationSnapshot readSnapshot(const Json& value, const AutomationLimits& limits, WireClock::time_point now) {
  fields(value, {"operation_id", "state", "abort_reason", "committed", "result_availability", "submitted_ago_ms",
      "updated_ago_ms", "progress", "completed_ago_ms", "outcome"});
  OperationSnapshot result;
  result.operation_id = id(value.at("operation_id"));
  result.state = readEnum(value.at("state"), states);
  result.abort_reason = readEnum(value.at("abort_reason"), aborts);
  result.committed = boolean(value.at("committed"));
  result.result_availability = readEnum(value.at("result_availability"), availability);
  auto time = [&](const char* key) { return now - std::chrono::milliseconds{uint(value.at(key), 86400000)}; };
  result.submitted_at = time("submitted_ago_ms");
  result.updated_at = time("updated_ago_ms");
  require(result.updated_at >= result.submitted_at);
  if (!value.at("completed_ago_ms").is_null()) result.completed_at = time("completed_ago_ms");
  if (!value.at("outcome").is_null()) result.outcome = readResult(value.at("outcome"), limits, now);
  require(isTerminal(result.state) == result.completed_at.has_value() &&
          isTerminal(result.state) == result.outcome.has_value());
  if (result.outcome) {
    require(result.outcome->operation_id == result.operation_id &&
        *result.completed_at >= result.submitted_at && *result.completed_at <= result.updated_at);
    const auto expected = result.outcome->ok ? OperationState::Succeeded :
        result.outcome->error_code == ErrorCode::kCancelled ? OperationState::Cancelled :
        result.outcome->error_code == ErrorCode::kTimeout ? OperationState::TimedOut : OperationState::Failed;
    require(result.state == expected);
  }
  const auto& progress = value.at("progress");
  fields(progress, {"stage", "frames", "width", "height", "stop_reason"});
  result.progress = {string(progress.at("stage"), 1024), uint(progress.at("frames")),
      static_cast<int>(uint(progress.at("width"), INT32_MAX)), static_cast<int>(uint(progress.at("height"), INT32_MAX)),
      string(progress.at("stop_reason"), 1024)};
  return result;
}
Json controlJson(const AutomationControlOutput& control, const AutomationLimits& limits, WireClock::time_point now) {
  return std::visit([&](const auto& value) -> Json {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, std::monostate>) return {{"kind", "none"}};
    else if constexpr (std::is_same_v<T, OperationSnapshot>)
      return {{"kind", "operation"}, {"operation", snapshotJson(value, limits, now)}};
    else if constexpr (std::is_same_v<T, CancellationResult>)
      return {{"kind", "cancellation"}, {"target", targetJson(value.target)}, {"operation_id", value.operation_id},
          {"state", enumName(value.state, states)}, {"cancellation_requested", value.cancellation_requested}};
    else return {{"kind", "released"}, {"result_id", value.result_id}, {"already_released", value.already_released}};
  }, control);
}
AutomationControlOutput readControl(const Json& value, const AutomationLimits& limits, WireClock::time_point now) {
  require(value.is_object() && value.contains("kind"));
  const auto& kind = string(value.at("kind"), 32);
  if (kind == "none") { fields(value, {"kind"}); return std::monostate{}; }
  if (kind == "operation") {
    fields(value, {"kind", "operation"}); return readSnapshot(value.at("operation"), limits, now);
  }
  if (kind == "cancellation") {
    fields(value, {"kind", "target", "operation_id", "state", "cancellation_requested"});
    CancellationResult result{readTarget(value.at("target")), uint(value.at("operation_id")),
        readEnum(value.at("state"), states), boolean(value.at("cancellation_requested"))};
    if (const auto* target = std::get_if<OperationCancellation>(&result.target)) require(target->operation_id == result.operation_id);
    return result;
  }
  if (kind == "released") {
    fields(value, {"kind", "result_id", "already_released"});
    return ReleasedResult{id(value.at("result_id")), boolean(value.at("already_released"))};
  }
  throw Failure{WireError::InvalidMessage};
}

template <typename T> void putHandles(Json& value, const T& message, const AutomationLimits& limits) {
  if (message.result_handle) value["result_handle"] = textJson(message.result_handle->value, limits.max_opaque_handle_bytes, false);
  if (message.operation_handle) value["operation_handle"] = textJson(message.operation_handle->value, limits.max_opaque_handle_bytes, false);
}
template <typename T> void readHandles(const Json& value, T& message, const AutomationLimits& limits) {
  if (value.contains("result_handle")) message.result_handle = ResultHandle{string(value.at("result_handle"), limits.max_opaque_handle_bytes, false)};
  if (value.contains("operation_handle")) message.operation_handle = OperationHandle{string(value.at("operation_handle"), limits.max_opaque_handle_bytes, false)};
}
Json messageJson(const WireMessage& message, const AutomationLimits& limits, WireClock::time_point now) {
  return std::visit([&](const auto& value) -> Json {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, WireHello>) {
      require(value.wire_version == kWireVersion, WireError::UnsupportedVersion);
      require(value.role == HelloRole::Client || value.role == HelloRole::Server);
      require(value.capabilities.size() <= capabilities.size());
      Json result{{"type", "hello"}, {"wire_version", value.wire_version},
          {"role", value.role == HelloRole::Client ? "client" : "server"}, {"capabilities", capabilitiesJson(value.capabilities)}};
      if (value.application_epoch) result["application_epoch"] = *value.application_epoch;
      if (value.connection_generation) result["connection_generation"] = *value.connection_generation;
      if (value.limits) result["limits"] = limitsJson(*value.limits);
      return result;
    } else if constexpr (std::is_same_v<T, WireRequest>) return requestJson(value, limits);
    else if constexpr (std::is_same_v<T, WireResponse>) {
      Json result{{"type", "response"}, {"rpc_id", rpcJson(value.rpc_id, limits)},
          {"result", resultJson(value.response.result, limits, now)},
          {"control", controlJson(value.response.control, limits, now)}};
      putHandles(result, value, limits); return result;
    } else {
      require(value.kind == EventKind::Progress || value.kind == EventKind::Completion);
      Json result{{"type", value.kind == EventKind::Progress ? "operation_progress" : "operation_completed"},
          {"operation", snapshotJson(value.operation, limits, now)}};
      putHandles(result, value, limits); return result;
    }
  }, message);
}
WireMessage readMessage(const Json& value, const AutomationLimits& limits, WireClock::time_point now) {
  require(value.is_object() && value.contains("type"));
  const auto& type = string(value.at("type"), 32, false);
  if (type == "hello") {
    require(value.contains("wire_version"));
    require(uint(value.at("wire_version"), UINT32_MAX) == kWireVersion, WireError::UnsupportedVersion);
    fields(value, {"type", "wire_version", "role", "capabilities"}, {"application_epoch", "connection_generation", "limits"});
    WireHello result;
    const auto& role = string(value.at("role"), 16);
    require(role == "client" || role == "server");
    result.role = role == "client" ? HelloRole::Client : HelloRole::Server;
    result.capabilities = readCapabilities(value.at("capabilities"));
    if (result.role == HelloRole::Server) {
      require(value.contains("application_epoch") && value.contains("connection_generation") && value.contains("limits"));
      result.application_epoch = id(value.at("application_epoch"));
      result.connection_generation = id(value.at("connection_generation"));
      result.limits = readLimits(value.at("limits"));
    } else require(!value.contains("application_epoch") && !value.contains("connection_generation") && !value.contains("limits"));
    return result;
  }
  if (type == "response") {
    fields(value, {"type", "rpc_id", "result", "control"}, {"result_handle", "operation_handle"});
    WireResponse result;
    result.rpc_id = readRpc(value.at("rpc_id"), limits);
    result.response.result = readResult(value.at("result"), limits, now);
    result.response.control = readControl(value.at("control"), limits, now);
    std::visit([&](const auto& control) {
      using T = std::decay_t<decltype(control)>;
      if constexpr (!std::is_same_v<T, std::monostate>) {
        require(result.response.result.ok);
        if constexpr (std::is_same_v<T, ReleasedResult>) require(result.response.result.operation_id == 0);
        else require(result.response.result.operation_id == control.operation_id);
      }
    }, result.response.control);
    readHandles(value, result, limits);
    return result;
  }
  if (type == "operation_progress" || type == "operation_completed") {
    fields(value, {"type", "operation"}, {"result_handle", "operation_handle"});
    WireEvent result;
    result.kind = type == "operation_progress" ? EventKind::Progress : EventKind::Completion;
    result.operation = readSnapshot(value.at("operation"), limits, now);
    require(isTerminal(result.operation.state) == (result.kind == EventKind::Completion));
    readHandles(value, result, limits);
    return result;
  }
  return readRequest(value, type, limits);
}

class BoundedBuffer final : public std::streambuf {
 public:
  explicit BoundedBuffer(std::size_t limit) : limit_(limit) {}
  std::string bytes;
 protected:
  int_type overflow(int_type character) override {
    if (traits_type::eq_int_type(character, traits_type::eof())) return traits_type::not_eof(character);
    require(bytes.size() < limit_, WireError::InvalidFrameLength);
    bytes.push_back(traits_type::to_char_type(character)); return character;
  }
  std::streamsize xsputn(const char* data, std::streamsize count) override {
    require(count >= 0 && static_cast<std::size_t>(count) <= limit_ - bytes.size(), WireError::InvalidFrameLength);
    bytes.append(data, static_cast<std::size_t>(count)); return count;
  }
 private:
  std::size_t limit_;
};
}  // namespace

const char* wireErrorSymbol(WireError error) noexcept {
  switch (error) {
    case WireError::None: return "None";
    case WireError::InvalidConfiguration: return "InvalidConfiguration";
    case WireError::InvalidFrameLength: return "InvalidFrameLength";
    case WireError::TruncatedFrame: return "TruncatedFrame";
    case WireError::InvalidJson: return "InvalidJson";
    case WireError::DuplicateKey: return "DuplicateKey";
    case WireError::DepthLimit: return "DepthLimit";
    case WireError::InvalidMessage: return "InvalidMessage";
    case WireError::UnsupportedVersion: return "UnsupportedVersion";
    case WireError::ResourceLimit: return "ResourceLimit";
    case WireError::ConsumerRejected: return "ConsumerRejected";
  }
  return "InvalidMessage";
}
DecodedMessage decodeBody(std::string_view body, const AutomationLimits& limits, WireClock::time_point now) noexcept {
  try {
    require(validLimits(limits), WireError::InvalidConfiguration);
    checkClock(now);
    return {WireError::None, readMessage(parse(body, limits), limits, now)};
  } catch (const Failure& failure) { return {failure.error, {}}; }
  catch (const std::bad_alloc&) { return {WireError::ResourceLimit, {}}; }
  catch (const Json::parse_error&) { return {WireError::InvalidJson, {}}; }
  catch (...) { return {WireError::InvalidMessage, {}}; }
}
EncodedFrame encodeFrame(const WireMessage& message, const AutomationLimits& limits, WireClock::time_point now) noexcept {
  try {
    require(validLimits(limits), WireError::InvalidConfiguration);
    checkClock(now);
    const auto value = messageJson(message, limits, now);
    // Apply the same schema rules to producers, including enum/correlation and
    // terminal consistency; malformed producer output never reaches a pipe.
    (void)readMessage(value, limits, now);
    BoundedBuffer buffer{static_cast<std::size_t>(limits.max_frame_bytes)};
    std::ostream stream{&buffer};
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream << value;
    Preflight sax{limits};
    const bool valid = Json::sax_parse(buffer.bytes.begin(), buffer.bytes.end(), &sax);
    require(valid, sax.error);
    const auto size = static_cast<std::uint32_t>(buffer.bytes.size());
    std::string frame(4, '\0');
    for (unsigned i = 0; i < 4; ++i) frame[i] = static_cast<char>((size >> (i * 8)) & 0xff);
    frame += buffer.bytes;
    return {WireError::None, std::move(frame)};
  } catch (const Failure& failure) { return {failure.error, {}}; }
  catch (const std::bad_alloc&) { return {WireError::ResourceLimit, {}}; }
  catch (...) { return {WireError::InvalidMessage, {}}; }
}
FrameDecoder::FrameDecoder(AutomationLimits limits) : limits_(limits) {
  if (!validLimits(limits_)) error_ = WireError::InvalidConfiguration;
}
WireError FrameDecoder::feed(std::string_view bytes, const Consumer& consumer, WireClock::time_point now) noexcept {
  if (error_ != WireError::None) return error_;
  try {
    require(static_cast<bool>(consumer), WireError::ConsumerRejected);
    while (!bytes.empty()) {
      while (header_size_ < 4 && !bytes.empty()) {
        header_[header_size_++] = static_cast<unsigned char>(bytes.front());
        bytes.remove_prefix(1);
      }
      if (header_size_ != 4) break;
      if (length_ == 0) {
        for (unsigned i = 0; i < 4; ++i) length_ |= std::uint32_t{header_[i]} << (i * 8);
        require(length_ > 0 && length_ <= limits_.max_frame_bytes, WireError::InvalidFrameLength);
        body_.reserve(length_); // Only after validating the full length prefix.
      }
      const auto count = (std::min)(bytes.size(), static_cast<std::size_t>(length_) - body_.size());
      body_.append(bytes.data(), count);
      bytes.remove_prefix(count);
      if (body_.size() == length_) {
        auto decoded = decodeBody(body_, limits_, now);
        require(static_cast<bool>(decoded), decoded.error);
        require(consumer(std::move(*decoded.message)), WireError::ConsumerRejected);
        body_.clear(); header_size_ = 0; length_ = 0;
      }
    }
  } catch (const Failure& failure) { error_ = failure.error; }
  catch (const std::bad_alloc&) { error_ = WireError::ResourceLimit; }
  catch (...) { error_ = WireError::ConsumerRejected; }
  return error_;
}
WireError FrameDecoder::finish() noexcept {
  if (error_ == WireError::None && (header_size_ || !body_.empty())) error_ = WireError::TruncatedFrame;
  return error_;
}
}  // namespace qingying::ipc
