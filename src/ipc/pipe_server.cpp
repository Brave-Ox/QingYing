#include "qingying/ipc/pipe_server.h"
#include "pipe_io.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace qingying::ipc {
namespace {
bool controlLane(const AutomationRequest& request) {
  const auto* execute = std::get_if<ExecuteActionRequest>(&request.payload);
  return execute ? std::holds_alternative<StatusRequest>(execute->payload)
                 : !std::holds_alternative<BeginLongShotRequest>(request.payload);
}
struct QueuedRequest {
  WireRequest wire;
  detail::Deadline submitted_at;
};
struct PendingRequest { RpcId rpc; bool control; };
struct Session {
  explicit Session(PipeOptions options) : stream(std::move(options)),
      ready(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      cleaned(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!ready || !cleaned) throw std::runtime_error("session events");
  }
  detail::PipeStream stream;
  detail::Handle ready;
  detail::Handle cleaned;
  bool live{true};
  bool hello{false};
  bool cleanup_done{false};
  std::optional<TrustedAutomationContext> context;
  std::deque<QueuedRequest> requests[2];
  std::map<RequestId, PendingRequest> pending;
};
struct Shared {
  std::mutex mutex;
  PipeServer::Hooks hooks;
  PipeOptions options;
  bool stopping{false};
  std::size_t in_flight[2]{};
  std::vector<std::shared_ptr<Session>> sessions;
};
// mutex held: invalidate the transport before scheduler revocation. Both
// scheduler.submit and scheduler.disconnect share scheduler's admission lock.
void revoke(Shared& shared, Session& session) {
  if (!session.live) return;
  session.live = false;
  session.stream.cancel();
  for (const auto& item : session.pending) --shared.in_flight[item.second.control];
  session.pending.clear();
  session.requests[0].clear();
  session.requests[1].clear();
  if (session.context) shared.hooks.revoke(*session.context);
  if (!shared.stopping && shared.hooks.wake) shared.hooks.wake();
}
}
struct PipeServer::Impl {
  explicit Impl(Hooks hooks, PipeOptions options) : shared(std::make_shared<Shared>()),
      stop_event(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    if (!detail::validOptions(options) || !hooks.connect || !hooks.submit ||
        !hooks.revoke || !hooks.disconnect || !stop_event)
      throw std::invalid_argument("pipe server configuration");
    shared->hooks = std::move(hooks);
    shared->options = std::move(options);
  }
  void checkThread() const {
    if (owner != std::this_thread::get_id()) throw std::logic_error("pipe owner thread");
  }
  void workerExited() noexcept {
    {
      std::lock_guard<std::mutex> lock(worker_mutex);
      if (active_workers > 0) --active_workers;
    }
    workers_done.notify_all();
  }
  void run(HANDLE pipe) noexcept {
    try {
      while (WaitForSingleObject(stop_event.get(), 0) == WAIT_TIMEOUT) {
        if (!detail::pipeConnect(pipe, stop_event.get())) {
          const auto connect_error = GetLastError();
          if (WaitForSingleObject(stop_event.get(), 0) != WAIT_TIMEOUT) break;
          // A client may open and close an instance before ConnectNamedPipe
          // starts. Reset that instance instead of permanently losing a slot.
          if (connect_error == ERROR_NO_DATA || connect_error == ERROR_BROKEN_PIPE ||
              connect_error == ERROR_PIPE_NOT_CONNECTED) {
            DisconnectNamedPipe(pipe);
            continue;
          }
          throw std::runtime_error("pipe connect failed");
        }
        auto session = std::make_shared<Session>(shared->options);
        HANDLE duplicate = nullptr;
        if (!DuplicateHandle(GetCurrentProcess(), pipe, GetCurrentProcess(), &duplicate,
                             0, FALSE, DUPLICATE_SAME_ACCESS)) break;
        session->stream.pipe.reset(duplicate);
        {
          std::lock_guard<std::mutex> lock(shared->mutex);
          if (shared->stopping) break;
          shared->sessions.push_back(session);
        }
        const auto deadline = detail::after(shared->options.handshake_timeout);
        auto first = session->stream.read(deadline);
        const auto* hello = first ? std::get_if<WireHello>(&*first) : nullptr;
        if (hello && hello->role == HelloRole::Client &&
            detail::verifyPeer(pipe, true, identity) && session->stream.startWriter()) {
          {
            std::lock_guard<std::mutex> lock(shared->mutex);
            session->hello = true;
            if (shared->hooks.wake) shared->hooks.wake();
          }
          HANDLE events[] = {session->stream.stopEvent(), session->ready.get()};
          const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              deadline - std::chrono::steady_clock::now()).count();
          if (ms > 0 && WaitForMultipleObjects(2, events, FALSE,
              static_cast<DWORD>(ms)) == WAIT_OBJECT_0 + 1) {
            while (!session->stream.stopped()) {
              auto message = session->stream.read(detail::Deadline::max());
              auto* wire = message ? std::get_if<WireRequest>(&*message) : nullptr;
              if (!wire) break;
              const bool control = controlLane(wire->request);
              std::lock_guard<std::mutex> lock(shared->mutex);
              if (!session->live || shared->stopping) break;
              const auto& limits = shared->options.limits;
              std::size_t local = 0;
              bool duplicate_id = false;
              for (const auto& item : session->pending) {
                if (item.second.control == control) ++local;
                duplicate_id |= item.first == wire->request.request_id || item.second.rpc == wire->rpc_id;
              }
              const auto local_max = control ? limits.max_control_requests_per_connection
                                             : limits.max_queued_requests_per_connection;
              const auto global_max = control ? limits.max_control_requests_global
                                              : limits.max_queued_requests_global;
              // A peer that exceeds finite admission is disconnected. No
              // unbounded rejection queue and no ambiguous duplicate RPC IDs.
              if (duplicate_id || local >= local_max || shared->in_flight[control] >= global_max) break;
              session->pending.emplace(wire->request.request_id, PendingRequest{wire->rpc_id, control});
              ++shared->in_flight[control];
              session->requests[control].push_back({std::move(*wire), std::chrono::steady_clock::now()});
              if (shared->hooks.wake) shared->hooks.wake();
            }
          }
        }
        {
          std::lock_guard<std::mutex> lock(shared->mutex);
          revoke(*shared, *session);
        }
        session->stream.joinWriter();
        DisconnectNamedPipe(pipe);
        // Keep the bounded slot until owner cleanup releases its scope. stop
        // wakes this wait without depending on UI pumping.
        HANDLE events[] = {stop_event.get(), session->cleaned.get()};
        WaitForMultipleObjects(2, events, FALSE, INFINITE);
        {
          std::lock_guard<std::mutex> lock(shared->mutex);
          if (shared->stopping) break;
          auto& sessions = shared->sessions;
          sessions.erase(std::remove(sessions.begin(), sessions.end(), session), sessions.end());
        }
      }
    } catch (...) {
      // Allocation or hook failures must close admission rather than escape
      // a worker. stop() will reclaim owner-side records.
      std::lock_guard<std::mutex> lock(shared->mutex);
      shared->stopping = true;
      SetEvent(stop_event.get());
      for (auto& session : shared->sessions) revoke(*shared, *session);
    }
  }
  std::shared_ptr<Shared> shared;
  detail::Identity identity;
  std::wstring name;
  detail::Handle stop_event;
  std::vector<std::unique_ptr<detail::Handle>> pipes;
  std::vector<std::thread> workers;
  std::mutex worker_mutex;
  std::condition_variable workers_done;
  std::size_t active_workers{0};
  std::thread::id owner{std::this_thread::get_id()};
  DWORD error{ERROR_SUCCESS};
  bool started{false};
};
PipeServer::PipeServer(Hooks hooks, PipeOptions options)
    : impl_(std::make_unique<Impl>(std::move(hooks), std::move(options))) {}
PipeServer::~PipeServer() { stop(); }
bool PipeServer::start() {
  auto& impl = *impl_;
  impl.checkThread();
  if (impl.started) return false;
  impl.started = true;
  if (!detail::currentIdentity(impl.identity)) { impl.error = ERROR_ACCESS_DENIED; return false; }
  impl.name = detail::pipeName(impl.identity, impl.shared->options.test_suffix);
  if (impl.name.empty()) { impl.error = ERROR_INVALID_NAME; return false; }
  detail::PipeSecurity security(impl.identity);
  if (!security.attributes()) { impl.error = ERROR_ACCESS_DENIED; return false; }
  try {
    const auto slots = impl.shared->options.limits.max_connections;
    for (std::uint32_t i = 0; i < slots; ++i) {
      auto pipe = std::make_unique<detail::Handle>(CreateNamedPipeW(impl.name.c_str(),
          PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | (i == 0 ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
          PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
          slots, 4096, 4096, 0, security.attributes()));
      if (!*pipe) { impl.error = GetLastError(); stop(); return false; }
      impl.pipes.push_back(std::move(pipe));
    }
    // Keep all original handles, especially the first, until every worker has
    // stopped: no close/recreate gap in which another process can seize name.
    for (const auto& pipe : impl.pipes) {
      {
        std::lock_guard<std::mutex> lock(impl.worker_mutex);
        ++impl.active_workers;
      }
      try {
        impl.workers.emplace_back([&impl, handle = pipe->get()] {
          impl.run(handle);
          impl.workerExited();
        });
      } catch (...) {
        impl.workerExited();
        throw;
      }
    }
    return true;
  } catch (...) { impl.error = ERROR_NOT_ENOUGH_MEMORY; stop(); return false; }
}
void PipeServer::drain() {
  auto& impl = *impl_;
  impl.checkThread();
  auto shared = impl.shared;
  std::vector<std::shared_ptr<Session>> sessions;
  { std::lock_guard<std::mutex> lock(shared->mutex); sessions = shared->sessions; }
  for (auto& session : sessions) {
    bool connect = false;
    {
      std::lock_guard<std::mutex> lock(shared->mutex);
      connect = !shared->stopping && session->live && session->hello && !session->context;
    }
    if (connect) {
      auto context = shared->hooks.connect();
      std::lock_guard<std::mutex> lock(shared->mutex);
      if (context && context->valid()) {
        session->context = *context;
        if (session->live && !shared->stopping) {
          WireHello hello;
          hello.role = HelloRole::Server;
          hello.application_epoch = context->connection.application_epoch;
          hello.connection_generation = context->connection.generation;
          hello.limits = shared->options.limits;
          session->stream.send(hello);
          SetEvent(session->ready.get());
        } else shared->hooks.revoke(*context);
      } else revoke(*shared, *session);
    }
    // Prioritize control requests; process only the finite snapshot admitted
    // before this drain, so a producer cannot keep the UI inside an endless loop.
    for (int lane : {1, 0}) {
      std::size_t count = 0;
      { std::lock_guard<std::mutex> lock(shared->mutex); count = session->requests[lane].size(); }
      while (count--) {
        QueuedRequest request;
        TrustedAutomationContext context;
        {
          std::lock_guard<std::mutex> lock(shared->mutex);
          if (!session->live || !session->context || session->requests[lane].empty()) break;
          request = std::move(session->requests[lane].front());
          session->requests[lane].pop_front();
          context = *session->context;
          context.submitted_at = request.submitted_at;
        }
        const auto id = request.wire.request.request_id;
        std::weak_ptr<Session> weak = session;
        auto delivered = std::make_shared<std::atomic<bool>>(false);
        shared->hooks.submit(context, std::move(request.wire.request),
            [shared, weak, id, delivered](AutomationResponse response) {
          if (delivered->exchange(true)) return;
          auto target = weak.lock();
          if (!target) return;
          std::lock_guard<std::mutex> lock(shared->mutex);
          auto found = target->pending.find(id);
          if (!target->live || found == target->pending.end()) return;
          if (!sameConnection(response.connection, target->context->connection) ||
              response.result.request_id != id) { revoke(*shared, *target); return; }
          WireResponse wire;
          wire.rpc_id = found->second.rpc;
          wire.response = std::move(response);
          --shared->in_flight[found->second.control];
          target->pending.erase(found);
          if (!target->stream.send(wire)) revoke(*shared, *target);
        });
      }
    }
    std::optional<TrustedAutomationContext> cleanup;
    {
      std::lock_guard<std::mutex> lock(shared->mutex);
      if (!session->live && !session->cleanup_done) {
        cleanup = session->context;
        session->cleanup_done = true;
      } else continue;
    }
    if (cleanup) shared->hooks.disconnect(*cleanup);
    SetEvent(session->cleaned.get());
  }
}
void PipeServer::stopAccepting() noexcept {
  auto& impl = *impl_;
  impl.checkThread();
  {
    std::lock_guard<std::mutex> lock(impl.shared->mutex);
    impl.shared->stopping = true;
    for (auto& session : impl.shared->sessions) revoke(*impl.shared, *session);
    SetEvent(impl.stop_event.get());
  }
}
bool PipeServer::joinUntil(
    std::chrono::steady_clock::time_point deadline) noexcept {
  auto& impl = *impl_;
  impl.checkThread();
  {
    std::unique_lock<std::mutex> lock(impl.worker_mutex);
    if (impl.active_workers != 0 &&
        impl.workers_done.wait_until(lock, deadline) ==
            std::cv_status::timeout &&
        impl.active_workers != 0) {
      return false;
    }
  }
  for (auto& worker : impl.workers) if (worker.joinable()) worker.join();
  impl.workers.clear();
  drain();
  impl.shared->sessions.clear();
  impl.pipes.clear();
  return true;
}
std::string PipeServer::diagnosticSnapshot() const {
  auto& impl = *impl_;
  std::size_t active_workers = 0;
  {
    std::lock_guard<std::mutex> lock(impl.worker_mutex);
    active_workers = impl.active_workers;
  }
  std::size_t queue_length = 0;
  std::size_t pending_requests = 0;
  RequestId request_id = 0;
  {
    std::lock_guard<std::mutex> lock(impl.shared->mutex);
    for (const auto& session : impl.shared->sessions) {
      queue_length += session->requests[0].size();
      queue_length += session->requests[1].size();
      pending_requests += session->pending.size();
      if (request_id == 0 && !session->pending.empty()) {
        request_id = session->pending.begin()->first;
      }
    }
  }
  return "thread=pipe_worker request_id=" + std::to_string(request_id) +
         " plugin_id=n/a queue_length=" + std::to_string(queue_length) +
         " pending_requests=" + std::to_string(pending_requests) +
         " active_workers=" + std::to_string(active_workers) +
         " last_progress=" +
         (active_workers == 0 ? std::string("workers_stopped")
                              : std::string("waiting_for_io"));
}
void PipeServer::stop() noexcept {
  stopAccepting();
  (void)joinUntil((std::chrono::steady_clock::time_point::max)());
}
std::wstring PipeServer::name() const { return impl_->name; }
std::uint32_t PipeServer::lastError() const noexcept { return impl_->error; }
}  // namespace qingying::ipc
