#include "qingying/mcp/mcp_protocol_session.h"
#include "tool_catalog.h"
#include <deque>
#include <map>
#include <mutex>
#include <set>

namespace qingying::mcp {
namespace {
constexpr const char* profile = "2025-11-25";
// SAX preflight bounds depth before DOM allocation and detects decoded duplicate
// keys. JSON itself is parsed by the pinned dependency, not a custom parser.
struct Preflight : nlohmann::json_sax<Json> {
  explicit Preflight(const AutomationLimits& limits) : limits(limits) {}
  AutomationLimits limits;
  std::vector<std::set<std::string>> levels;
  bool null() override { return true; }
  bool boolean(bool) override { return true; }
  bool number_integer(number_integer_t) override { return true; }
  bool number_unsigned(number_unsigned_t) override { return true; }
  bool number_float(number_float_t, const string_t&) override { return true; }
  bool string(string_t& value) override { return value.size() <= limits.max_frame_bytes; }
  bool binary(binary_t&) override { return false; }
  bool start_object(std::size_t) override { levels.emplace_back(); return levels.size() <= limits.max_json_depth; }
  bool key(string_t& value) override { return string(value) && levels.back().insert(value).second; }
  bool end_object() override { levels.pop_back(); return true; }
  bool start_array(std::size_t size) override { return start_object(size); }
  bool end_array() override { return end_object(); }
  bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception&) override { return false; }
};
bool validId(const Json& id, const AutomationLimits& limits) {
  return id.is_number_integer() || (id.is_string() &&
      id.get_ref<const std::string&>().size() <= limits.max_request_key_bytes &&
      id.get_ref<const std::string&>().find('\0') == std::string::npos);
}
bool text(const Json& object, const char* key, std::size_t limit) {
  return object.contains(key) && object[key].is_string() &&
      !object[key].get_ref<const std::string&>().empty() &&
      object[key].get_ref<const std::string&>().size() <= limit &&
      object[key].get_ref<const std::string&>().find('\0') == std::string::npos;
}
}
struct McpProtocolSession::State {
  struct Pending { RequestId request; bool cancelled{false}; };
  std::shared_ptr<IAutomationClient> client;
  AutomationLimits limits;
  std::mutex mutex, input;
  enum class Phase { New, Initialized, Ready } phase{Phase::New};
  bool stopped{false}, client_closed{false};
  ClientInfo info;
  RequestId next_id{0};
  std::map<std::string, Pending> pending;
  std::set<RequestId> cancellations;
  std::deque<std::string> output;
  std::uint64_t output_bytes{0};
  void emit(Json message) {
    if (stopped) return;
    auto line = message.dump();
    line += '\n';
    if (line.size() > limits.max_frame_bytes || output.size() >= limits.max_output_frames_per_connection ||
        line.size() > limits.max_output_bytes_per_connection - output_bytes) {
      stopped = true; output.clear(); output_bytes = 0; return;
    }
    output_bytes += line.size(); output.push_back(std::move(line));
  }
  void error(const Json& id, int code, const char* message) {
    emit({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
  }
  void result(const Json& id, Json value) {
    emit({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(value)}});
  }
  RequestId allocate() {
    if (next_id == UINT64_MAX) { stopped = true; return 0; }
    return ++next_id;
  }
};
McpProtocolSession::McpProtocolSession(std::shared_ptr<IAutomationClient> client, AutomationLimits limits)
    : state_(std::make_shared<State>()) {
  if (!limits.valid() || limits.max_frame_bytes > 65536 || limits.max_json_depth > 16)
    throw std::invalid_argument("MCP limits");
  state_->client = std::move(client); state_->limits = limits;
}
McpProtocolSession::~McpProtocolSession() { close(); }
void McpProtocolSession::receive(std::string_view line) {
  auto state = state_;
  // Serialize ingress through actual submit, ensuring cancellation can never
  // reach the client before the corresponding originating submit.
  std::lock_guard<std::mutex> input(state->input);
  std::unique_lock<std::mutex> lock(state->mutex);
  if (state->stopped) return;
  try {
    if (line.size() > state->limits.max_frame_bytes) { state->stopped = true; return; }
    Preflight sax(state->limits);
    if (line.empty() || static_cast<unsigned char>(line.front()) == 0xef ||
        !Json::sax_parse(line.begin(), line.end(), &sax)) {
      state->error(nullptr, -32700, "Parse error"); return;
    }
    const auto message = Json::parse(line.begin(), line.end());
    if (!message.is_object()) { state->error(nullptr, -32600, "Invalid Request"); return; }
    const bool notification = !message.contains("id");
    Json id = notification ? Json(nullptr) : message["id"];
    if ((!notification && !validId(id, state->limits)) || message.value("jsonrpc", Json()) != "2.0" ||
        !text(message, "method", 128) || message.contains("result") || message.contains("error")) {
      state->error(nullptr, -32600, "Invalid Request"); return;
    }
    const auto method = message["method"].get<std::string>();
    const auto params = message.value("params", Json::object());
    if (!notification && state->pending.count(id.dump())) {
      // Closing is unambiguous: never send two responses for one live RPC id.
      state->stopped = true; state->output.clear(); state->output_bytes = 0; return;
    }
    if (notification) {
      if (method == "notifications/initialized" && params.is_object() &&
          state->phase == State::Phase::Initialized) state->phase = State::Phase::Ready;
      if (method != "notifications/cancelled" || !params.is_object() ||
          !params.contains("requestId") || !validId(params["requestId"], state->limits)) return;
      auto found = state->pending.find(params["requestId"].dump());
      if (found == state->pending.end() || found->second.cancelled) return;
      // Suppress the original response independently of cancellation success.
      found->second.cancelled = true;
      if (state->cancellations.size() >= state->limits.max_control_requests_per_connection) {
        state->stopped = true; return;
      }
      AutomationRequest request;
      request.request_id = state->allocate();
      if (!request.request_id) return;
      request.payload = CancelOperationRequest{RequestCancellation{found->second.request}};
      state->cancellations.insert(request.request_id);
      const auto request_id = request.request_id;
      auto client = state->client;
      lock.unlock();
      auto complete = [state, request_id](AutomationResponse) {
        std::lock_guard<std::mutex> guard(state->mutex); state->cancellations.erase(request_id);
      };
      try { if (client) client->submit(std::move(request), complete); else complete({}); }
      catch (...) { complete({}); }
      return;
    }
    if (!params.is_object()) { state->error(id, -32602, "Invalid params"); return; }
    if (method == "ping") { state->result(id, Json::object()); return; }
    if (method == "initialize") {
      if (state->phase != State::Phase::New) { state->error(id, -32600, "Already initialized"); return; }
      if (!text(params, "protocolVersion", 64) || !params.contains("capabilities") ||
          !params["capabilities"].is_object() || !params.contains("clientInfo") ||
          !text(params["clientInfo"], "name", 128) || !text(params["clientInfo"], "version", 128)) {
        state->error(id, -32602, "Invalid initialize params"); return;
      }
      state->info = {params["clientInfo"]["name"], params["clientInfo"]["version"], params["protocolVersion"], profile};
      state->phase = State::Phase::Initialized;
      state->result(id, {{"protocolVersion", profile}, {"capabilities", {{"tools", {{"listChanged", false}}}}},
          {"serverInfo", {{"name", "QingYing"}, {"version", "0.1.0"}}}});
      return;
    }
    if (method != "tools/list" && method != "tools/call") {
      state->error(id, -32601, "Method not found"); return;
    }
    if (state->phase != State::Phase::Ready) { state->error(id, -32600, "Session not initialized"); return; }
    if (method == "tools/list") {
      if (params.contains("cursor")) { state->error(id, -32602, "Invalid cursor"); return; }
      Json list = Json::array();
      for (const auto& tool : tools()) list.push_back(tool.descriptor(state->limits));
      state->result(id, {{"tools", list}}); return;
    }
    if (!text(params, "name", 128)) { state->error(id, -32602, "Missing tool name"); return; }
    const auto* tool = findTool(params["name"]);
    if (!tool) { state->error(id, -32602, "Unknown tool"); return; }
    if (params.contains("task")) { state->error(id, -32602, "Task execution is not supported"); return; }
    if (state->pending.size() >= state->limits.max_control_requests_per_connection) {
      state->result(id, toolFailure(ErrorCode::kResourceLimit, "Too many in-flight calls")); return;
    }
    auto arguments = params.value("arguments", Json::object());
    const auto request_id = state->allocate();
    if (!request_id) return;
    AutomationRequest request;
    try { request = tool->decode(arguments, request_id, state->limits); }
    catch (...) { state->error(id, -32602, "Arguments do not match inputSchema"); return; }
    state->pending.emplace(id.dump(), State::Pending{request_id});
    auto client = state->client;
    lock.unlock();
    auto complete = [state, id, request_id, tool, arguments](AutomationResponse response) {
      std::lock_guard<std::mutex> guard(state->mutex);
      const auto found = state->pending.find(id.dump());
      if (found == state->pending.end() || found->second.request != request_id) return;
      const bool cancelled = found->second.cancelled;
      state->pending.erase(found);
      if (cancelled || state->stopped) return;
      try {
        if (response.result.request_id != request_id)
          state->result(id, toolFailure(ErrorCode::kUnknown, "Mismatched automation response"));
        else state->result(id, tool->encode(response, arguments, state->limits));
      } catch (...) { state->stopped = true; }
    };
    AutomationResponse failure;
    failure.transport_available = false;
    failure.result.request_id = request_id;
    failure.result.error_code = ErrorCode::kNotReady;
    try { if (client) client->submit(std::move(request), complete); else complete(failure); }
    catch (...) { complete(failure); }
  } catch (...) {
    if (!lock.owns_lock()) lock.lock();
    state->stopped = true;
  }
}
std::optional<std::string> McpProtocolSession::takeOutput() {
  std::lock_guard<std::mutex> lock(state_->mutex);
  if (state_->output.empty()) return std::nullopt;
  auto line = std::move(state_->output.front()); state_->output.pop_front();
  state_->output_bytes -= line.size(); return line;
}
bool McpProtocolSession::closed() const {
  std::lock_guard<std::mutex> lock(state_->mutex); return state_->stopped;
}
ClientInfo McpProtocolSession::clientInfo() const {
  std::lock_guard<std::mutex> lock(state_->mutex); return state_->info;
}
void McpProtocolSession::close() noexcept {
  auto state = state_;
  // Wait for ingress to finish submitting before closing the bound client.
  std::lock_guard<std::mutex> input(state->input);
  std::shared_ptr<IAutomationClient> client;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->stopped = true; state->pending.clear(); state->cancellations.clear();
    if (!state->client_closed) { state->client_closed = true; client = state->client; }
  }
  if (client) client->close();
}
}  // namespace qingying::mcp
