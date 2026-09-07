#include "qingying/ipc/pipe_automation_client.h"
#include "pipe_io.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <stdexcept>

namespace qingying::ipc {
namespace {
void deliver(AutomationCompletion completion, AutomationResponse response) noexcept {
  if (completion) { try { completion(std::move(response)); } catch (...) {} }
}
AutomationResponse failure(AutomationConnection connection, RequestId id, int code) {
  AutomationResponse response;
  response.connection = connection;
  response.result.request_id = id;
  response.result.error_code = code;
  return response;
}
bool controlLane(const AutomationRequest& request) {
  const auto* execute = std::get_if<ExecuteActionRequest>(&request.payload);
  return execute ? std::holds_alternative<StatusRequest>(execute->payload)
                 : !std::holds_alternative<BeginLongShotRequest>(request.payload);
}
struct PendingCompletion {
  AutomationCompletion callback;
  bool control;
};
}
struct PipeAutomationClient::Impl {
  explicit Impl(PipeOptions config) : options(std::move(config)), stream(options) {
    if (!detail::validOptions(options)) throw std::invalid_argument("pipe client configuration");
  }
  void settle() noexcept {
    std::map<RequestId, PendingCompletion> callbacks;
    AutomationConnection identity;
    {
      std::lock_guard<std::mutex> lock(mutex);
      accepting = false;
      callbacks.swap(pending);
      identity = connection;
    }
    for (auto& item : callbacks)
      deliver(std::move(item.second.callback), failure(identity, item.first, ErrorCode::kCancelled));
  }
  void run() noexcept {
    { std::lock_guard<std::mutex> lock(mutex); reader_id = std::this_thread::get_id(); }
    try {
      while (!stream.stopped()) {
        auto message = stream.read(detail::Deadline::max());
        auto* response = message ? std::get_if<WireResponse>(&*message) : nullptr;
        if (!response) { error = ERROR_INVALID_DATA; break; }
        AutomationCompletion completion;
        {
          std::lock_guard<std::mutex> lock(mutex);
          const auto* rpc = std::get_if<std::uint64_t>(&response->rpc_id);
          auto found = pending.find(response->response.result.request_id);
          if (!accepting || !rpc || *rpc != response->response.result.request_id || found == pending.end()) {
            error = ERROR_INVALID_DATA; break;
          }
          completion = std::move(found->second.callback);
          pending.erase(found);
          response->response.connection = connection;
        }
        deliver(std::move(completion), std::move(response->response));
      }
    } catch (...) { error = ERROR_NOT_ENOUGH_MEMORY; }
    stream.cancel();
    stream.joinWriter();
    settle();
    {
      std::lock_guard<std::mutex> lock(mutex);
      stream.pipe.reset();
      reader_done = true;
    }
    done.notify_all();
  }
  PipeOptions options;
  detail::PipeStream stream;
  std::mutex mutex;
  std::condition_variable done;
  std::map<RequestId, PendingCompletion> pending;
  AutomationConnection connection;
  std::thread::id reader_id;
  bool attempted{false};
  bool accepting{false};
  bool reader_done{true};
  std::atomic<DWORD> error{ERROR_SUCCESS};
};
PipeAutomationClient::PipeAutomationClient(PipeOptions options)
    : impl_(std::make_shared<Impl>(std::move(options))) {}
PipeAutomationClient::~PipeAutomationClient() { close(); }
bool PipeAutomationClient::connect() {
  auto state = impl_;
  // connect must finish before concurrent use. This method never silently
  // reconnects an object whose prior requests have an unknown outcome.
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->attempted || state->stream.stopped()) return false;
    state->attempted = true;
  }
  detail::Identity identity;
  if (!detail::currentIdentity(identity)) { state->error = ERROR_ACCESS_DENIED; return false; }
  auto name = detail::pipeName(identity, state->options.test_suffix);
  if (name.empty()) { state->error = ERROR_INVALID_NAME; return false; }
  const auto deadline = detail::after(state->options.handshake_timeout);
  for (;;) {
    state->stream.pipe.reset(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED |
        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
    if (state->stream.pipe) break;
    state->error = GetLastError();
    if (state->error != ERROR_PIPE_BUSY || state->stream.stopped() ||
        std::chrono::steady_clock::now() >= deadline) return false;
    WaitNamedPipeW(name.c_str(), 10);
  }
  try {
    if (!detail::verifyPeer(state->stream.pipe.get(), false, identity)) {
      state->error = ERROR_ACCESS_DENIED;
    } else if (state->stream.startWriter() && state->stream.send(WireHello{})) {
      auto message = state->stream.read(deadline);
      auto* hello = message ? std::get_if<WireHello>(&*message) : nullptr;
      if (hello && hello->role == HelloRole::Server && hello->application_epoch &&
          hello->connection_generation &&
          detail::verifyPeer(state->stream.pipe.get(), false, identity)) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->connection = {*hello->application_epoch, *hello->connection_generation};
        state->accepting = true;
        state->reader_done = false;
        try {
          std::thread([state] { state->run(); }).detach();
          state->error = ERROR_SUCCESS;
          return true;
        } catch (...) { state->accepting = false; state->reader_done = true; }
      }
      state->error = ERROR_INVALID_DATA;
    }
  } catch (...) { state->error = ERROR_NOT_ENOUGH_MEMORY; }
  state->stream.cancel();
  state->stream.joinWriter();
  state->stream.pipe.reset();
  return false;
}
AutomationConnection PipeAutomationClient::connection() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->connection;
}
std::uint32_t PipeAutomationClient::lastError() const noexcept { return impl_->error.load(); }
void PipeAutomationClient::submit(AutomationRequest request, AutomationCompletion completion) {
  auto state = impl_;
  const auto id = request.request_id;
  int code = ErrorCode::kCancelled;
  AutomationConnection identity;
  bool inserted = false;
  std::unique_lock<std::mutex> lock(state->mutex);
  try {
    identity = state->connection;
    if (state->accepting && !state->stream.stopped()) {
      code = ErrorCode::kInvalidArgument;
      if (validateAutomationRequest(request, state->options.limits).valid) {
        code = ErrorCode::kResourceLimit;
        const bool control = controlLane(request);
        const auto max = control ? state->options.limits.max_control_requests_per_connection
                                 : state->options.limits.max_queued_requests_per_connection;
        std::size_t count = 0;
        for (const auto& item : state->pending) if (item.second.control == control) ++count;
        if (count < max && !state->pending.count(id)) {
          // Register before waking the writer: a fast response can never beat
          // tracking. Keep the callback locally until insertion succeeds.
          state->pending.emplace(id, PendingCompletion{completion, control});
          inserted = true;
          WireRequest wire;
          wire.rpc_id = static_cast<std::uint64_t>(id);
          wire.request = std::move(request);
          if (state->stream.send(wire)) return;
          state->pending.erase(id);
        }
      }
    }
  } catch (...) {
    state->stream.cancel();
    if (inserted) state->pending.erase(id);
    code = ErrorCode::kResourceLimit;
  }
  lock.unlock();
  deliver(std::move(completion), failure(identity, id, code));
}
void PipeAutomationClient::close() noexcept {
  auto state = impl_;
  state->stream.cancel();
  bool on_reader = false;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->accepting = false;
    on_reader = state->reader_id == std::this_thread::get_id();
  }
  if (on_reader) {
    state->settle();
    return; // shared state retains buffers until this callback returns to run.
  }
  std::unique_lock<std::mutex> lock(state->mutex);
  state->done.wait(lock, [&] { return state->reader_done; });
}
}  // namespace qingying::ipc
