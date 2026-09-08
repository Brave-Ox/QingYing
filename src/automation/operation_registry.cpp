#include "qingying/automation/operation_registry.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#include <bcrypt.h>
#else
#include <random>
#endif

namespace qingying {
namespace {
using Clock = std::chrono::steady_clock;

Clock::time_point deadline(Clock::time_point now, std::chrono::milliseconds ttl) {
  if (ttl.count() <= 0) return now;
  using Wide = std::chrono::duration<long double>;
  if (Wide(ttl) >= Wide((Clock::duration::max)())) return (Clock::time_point::max)();
  const auto duration = std::chrono::duration_cast<Clock::duration>(ttl);
  if (now.time_since_epoch() > (Clock::duration::max)() - duration) {
    return (Clock::time_point::max)();
  }
  return now + duration;
}

ActionResult failure(int code, const char* message) {
  ActionResult result;
  result.error_code = code;
  result.message = message;
  return result;
}

bool takeBytes(std::uint64_t& remaining, std::size_t count, std::uint64_t unit = 1) {
  if (count > remaining / unit) return false;
  remaining -= static_cast<std::uint64_t>(count) * unit;
  return true;
}

bool boundedOutcome(const ActionResult& result, const AutomationLimits& limits) {
  auto remaining = limits.max_frame_bytes;
  if (!takeBytes(remaining, 128) || !takeBytes(remaining, result.message.capacity()) ||
      !takeBytes(remaining, result.data.capacity()) || !takeBytes(remaining, result.failure_stage.capacity())) {
    return false;
  }
  return std::visit([&](const auto& output) {
    using T = std::decay_t<decltype(output)>;
    if constexpr (std::is_same_v<T, SavedResult>) {
      return takeBytes(remaining, output.absolute_path.capacity(), sizeof(wchar_t));
    } else if constexpr (std::is_same_v<T, StatusInfo>) {
      if (!takeBytes(remaining, output.busy_reason.capacity()) ||
          !takeBytes(remaining, output.build_version.capacity()) ||
          !takeBytes(remaining, output.connection_reason.capacity()) ||
          !takeBytes(remaining, output.capabilities.capacity(), sizeof(std::string))) return false;
      for (const auto& capability : output.capabilities) {
        if (!takeBytes(remaining, capability.capacity())) return false;
      }
      return true;
    } else if constexpr (std::is_same_v<T, WindowCandidates>) {
      if (!takeBytes(remaining, output.candidates.capacity(), sizeof(WindowCandidate))) return false;
      for (const auto& candidate : output.candidates) {
        if (!takeBytes(remaining, candidate.title.capacity(), sizeof(wchar_t)) ||
            !takeBytes(remaining, candidate.window_token.capacity())) return false;
      }
      return true;
    } else return true;
  }, result.output);
}

bool boundedRequest(const ExecuteActionRequest& request, const AutomationLimits& limits) {
  if (request.request_key && request.request_key->size() > limits.max_request_key_bytes) return false;
  return std::visit([&](const auto& action) {
    using T = std::decay_t<decltype(action)>;
    if constexpr (std::is_same_v<T, SaveRequest>) {
      return action.path.size() <= limits.max_path_utf16_units &&
             action.path.size() <= limits.max_frame_bytes / sizeof(wchar_t) &&
             action.path.find(L'\0') == std::wstring::npos;
    } else if constexpr (std::is_same_v<T, CaptureWindowRequest>) {
      return action.window_query.size() <= limits.max_window_query_utf16_units &&
             action.window_query.size() <= limits.max_frame_bytes / sizeof(wchar_t) &&
             action.window_query.find(L'\0') == std::wstring::npos;
    } else return true;
  }, request.payload);
}

// Only Save/Copy/Pin accept keys. The fingerprint contains exact IDs and a
// lexical path, never RPC IDs or timeout. No filesystem lookup or case folding:
// later path authorization must still resolve the actual destination.
std::string canonical(const ExecuteActionRequest& request, std::uint64_t limit) {
  std::string value = std::to_string(static_cast<unsigned>(actionType(request.payload))) + ":";
  std::visit([&](const auto& action) {
    using T = std::decay_t<decltype(action)>;
    if constexpr (std::is_same_v<T, SaveRequest> || std::is_same_v<T, CopyRequest> ||
                  std::is_same_v<T, PinRequest>) {
      value += std::to_string(action.result.result_id) + ":";
      if constexpr (std::is_same_v<T, SaveRequest>) {
        value += action.overwrite ? "1:" : "0:";
        auto path = action.path;
        std::replace(path.begin(), path.end(), L'\\', L'/');
        path = std::filesystem::path(path).lexically_normal().generic_wstring();
        for (wchar_t unit : path) {
          value += std::to_string(static_cast<std::uint32_t>(unit)) + ",";
          if (value.size() > limit) break;
        }
      }
    }
  }, request.payload);
  return value;
}

bool transition(OperationState from, OperationState to) {
  if (isTerminal(to) || to == OperationState::Cancelling || to == OperationState::Queued) return false;
  if (from == to) return true;
  switch (from) {
    case OperationState::Queued:
      return to == OperationState::Running || to == OperationState::AwaitingUser;
    case OperationState::AwaitingUser: return to == OperationState::Running;
    case OperationState::Running:
      return to == OperationState::Paused || to == OperationState::Finalizing || to == OperationState::AwaitingUser;
    case OperationState::Paused:
      return to == OperationState::Running || to == OperationState::Finalizing;
    default: return false;
  }
}
}  // namespace

OperationControl::OperationControl(TrustedAutomationContext context, RequestId request_id,
    std::chrono::milliseconds timeout, OperationClock clock)
    : context_(std::move(context)), request_id_(request_id), timeout_(timeout),
      deadline_(deadline(context_.submitted_at, timeout)),
      clock_(clock ? std::move(clock) : OperationClock{Clock::now}) {
  if (!context_.valid() || request_id == kInvalidRequestId || timeout.count() <= 0 ||
      timeout > std::chrono::hours{24}) throw std::invalid_argument("invalid operation control");
}

void OperationControl::checkAbortLocked() {
  if (status_.settled || status_.committed || status_.abort_reason != AbortReason::None) return;
  if (clock_() >= deadline_) status_.abort_reason = AbortReason::Deadline;
  else if (context_.cancellation.isCancellationRequested()) status_.abort_reason = AbortReason::ClientCancel;
}

bool OperationControl::requestCancel(AbortReason reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  checkAbortLocked();
  if (reason == AbortReason::None || status_.committed || status_.settled ||
      status_.abort_reason != AbortReason::None) return false;
  status_.abort_reason = reason;
  return true;
}

bool OperationControl::tryCommit() {
  std::lock_guard<std::mutex> lock(mutex_);
  checkAbortLocked();
  if (status_.committed || status_.settled || status_.abort_reason != AbortReason::None) return false;
  status_.committed = true;
  return true;
}

OperationControlStatus OperationControl::status() {
  std::lock_guard<std::mutex> lock(mutex_);
  checkAbortLocked();
  return status_;
}

bool OperationControl::claim(const TrustedAutomationContext& context,
    const AutomationRequest& request, std::chrono::milliseconds timeout) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (claimed_ || status_.settled || status_.committed ||
      !sameConnection(context.connection, context_.connection) ||
      context.action.result_scope != context_.action.result_scope ||
      context.submitted_at != context_.submitted_at || request.request_id != request_id_ ||
      timeout != timeout_) return false;
  claimed_ = true;
  return true;
}

std::optional<ActionResult> OperationControl::settle(ActionResult outcome, bool requires_commit) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (status_.settled) return std::nullopt;
  checkAbortLocked();
  if (status_.abort_reason != AbortReason::None) {
    outcome.ok = false;
    outcome.error_code = status_.abort_reason == AbortReason::Deadline ? ErrorCode::kTimeout : ErrorCode::kCancelled;
    outcome.message = "operation aborted before commit";
    outcome.output = std::monostate{};
    outcome.data.clear();
  } else if (outcome.ok && requires_commit && !status_.committed) {
    outcome = failure(ErrorCode::kConflict, "success requires commit permission");
  }
  status_.settled = true;
  return outcome;
}

OperationRegistry::OperationRegistry(ApplicationEpoch epoch, AutomationLimits limits, OperationClock clock)
    : epoch_(epoch), limits_(limits), clock_(clock ? std::move(clock) : OperationClock{Clock::now}),
      ui_thread_(std::this_thread::get_id()) {
  if (epoch == kInvalidApplicationEpoch || !limits.valid() || limits.max_opaque_handle_bytes < 32) {
    throw std::invalid_argument("invalid operation registry policy or epoch");
  }
}

OperationRegistry::~OperationRegistry() {
  // Execution controls may outlive records; destruction closes commit rights.
  for (auto& entry : records_) entry.second.control->requestCancel(AbortReason::Shutdown);
}

void OperationRegistry::checkThread() const {
  if (std::this_thread::get_id() != ui_thread_) throw std::logic_error("operation registry requires UI thread");
}

OperationRegistry::Session* OperationRegistry::session(const TrustedAutomationContext& context, bool connected) {
  if (!context.valid() || context.connection.application_epoch != epoch_) return nullptr;
  const auto it = sessions_.find(context.connection.generation);
  if (it == sessions_.end() || it->second.scope != context.action.result_scope ||
      (connected && !it->second.connected)) return nullptr;
  return &it->second;
}

OperationRegistry::Record* OperationRegistry::record(
    const TrustedAutomationContext& context, OperationId id, bool connected) {
  if (!session(context, connected)) return nullptr;
  const auto it = records_.find(id);
  if (it == records_.end() || !sameConnection(context.connection, it->second.connection) ||
      context.action.result_scope != it->second.scope) return nullptr;
  return &it->second;
}

std::optional<TrustedAutomationContext> OperationRegistry::connect(ResultScopeId scope) {
  checkThread();
  sweep();
  if (scope == kInvalidResultScopeId || scope == kGuiResultScopeId ||
      sessions_.size() >= limits_.max_connections || next_generation_ == 0) return std::nullopt;
  for (const auto& entry : sessions_) if (entry.second.scope == scope) return std::nullopt;
  const auto generation = next_generation_++;
  sessions_.emplace(generation, Session{scope});
  TrustedAutomationContext context;
  context.connection = {epoch_, generation};
  context.action.result_scope = scope;
  context.submitted_at = clock_();
  return context;
}

void OperationRegistry::disconnect(const TrustedAutomationContext& context) {
  checkThread();
  auto* owner = session(context);
  if (!owner) return;
  owner->connected = false;
  owner->result_id = kInvalidResultId;
  owner->result_handle = {};
  owner->invalid_results.clear();
  for (auto& entry : records_) {
    auto& value = entry.second;
    if (!sameConnection(value.connection, context.connection)) continue;
    value.control->requestCancel(AbortReason::Disconnect);
    if (value.snapshot.result_availability == ResultAvailability::Available) {
      value.snapshot.result_availability = ResultAvailability::Released;
    }
    refresh(value);
  }
  trim();
}

std::string OperationRegistry::newHandle() const {
  constexpr char hex[] = "0123456789abcdef";
  for (int attempt = 0; attempt < 8; ++attempt) {
    std::array<unsigned char, 16> bytes{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                       BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) throw std::runtime_error("handle entropy failed");
#else
    std::random_device entropy;
    for (auto& byte : bytes) byte = static_cast<unsigned char>(entropy());
#endif
    std::string handle;
    handle.reserve(32);
    for (auto byte : bytes) { handle += hex[byte >> 4]; handle += hex[byte & 15]; }
    bool collision = false;
    for (const auto& entry : records_) collision |= entry.second.handle.value == handle;
    for (const auto& entry : sessions_) {
      collision |= entry.second.result_handle.value == handle;
      for (const auto& invalid : entry.second.invalid_results) collision |= invalid.handle.value == handle;
    }
    if (!collision) return handle;
  }
  throw std::runtime_error("handle collision limit");
}

OperationSubmission OperationRegistry::submission(const Record& value, bool reused) const {
  return {ErrorCode::kOk, reused, value.snapshot.operation_id, value.handle, value.control};
}

OperationSubmission OperationRegistry::begin(const TrustedAutomationContext& context,
    const AutomationRequest& request, std::shared_ptr<OperationControl> control) {
  checkThread();
  sweep();
  if (!session(context)) return {ErrorCode::kAccessDenied};
  if (context.submitted_at > clock_() || !validateAutomationRequest(request, limits_).valid) {
    return {ErrorCode::kInvalidArgument};
  }
  const auto* execute = std::get_if<ExecuteActionRequest>(&request.payload);
  if (!execute && !std::holds_alternative<BeginLongShotRequest>(request.payload)) {
    return {ErrorCode::kInvalidArgument};
  }
  if (execute && !boundedRequest(*execute, limits_)) return {ErrorCode::kResourceLimit};
  std::optional<std::string> key = execute ? execute->request_key : std::nullopt;
  std::string parameters;
  try { if (key) parameters = canonical(*execute, limits_.max_frame_bytes); }
  catch (const std::filesystem::filesystem_error&) { return {ErrorCode::kInvalidArgument}; }
  if (parameters.size() > limits_.max_frame_bytes) return {ErrorCode::kResourceLimit};
  if (key) {
    for (const auto& entry : records_) {
      const auto& value = entry.second;
      if (sameConnection(value.connection, context.connection) && value.request_key == key) {
        return value.canonical_parameters == parameters ? submission(value, true)
                                                        : OperationSubmission{ErrorCode::kConflict};
      }
    }
  }
  std::uint64_t active = 0, local = 0;
  for (const auto& entry : records_) if (!isTerminal(entry.second.snapshot.state)) {
    ++active;
    if (sameConnection(entry.second.connection, context.connection)) ++local;
  }
  if (active >= static_cast<std::uint64_t>(limits_.max_queued_requests_global) + limits_.max_desktop_operations ||
      local >= static_cast<std::uint64_t>(limits_.max_queued_requests_per_connection) + limits_.max_desktop_operations ||
      next_operation_ == 0) return {ErrorCode::kResourceLimit};
  const auto timeout = automationTimeout(request, limits_);
  if (!control) control = std::make_shared<OperationControl>(context, request.request_id, timeout, clock_);
  Record value;
  value.connection = context.connection;
  value.scope = context.action.result_scope;
  value.request_id = request.request_id;
  value.snapshot.operation_id = next_operation_++;
  value.snapshot.submitted_at = context.submitted_at;
  value.snapshot.updated_at = clock_();
  value.handle.value = newHandle();
  value.control = std::move(control);
  value.request_key = std::move(key);
  value.canonical_parameters = std::move(parameters);
  value.requires_commit = !execute || actionType(execute->payload) != ActionType::Status;
  const auto id = value.snapshot.operation_id;
  auto inserted = records_.emplace(id, std::move(value));
  if (!inserted.first->second.control->claim(context, request, timeout)) {
    records_.erase(inserted.first);
    return {ErrorCode::kConflict};
  }
  refresh(inserted.first->second);
  return submission(inserted.first->second, false);
}

void OperationRegistry::refresh(Record& value) {
  const auto control = value.control->status();
  if (isTerminal(value.snapshot.state)) return;
  if (control.committed != value.snapshot.committed || control.abort_reason != value.snapshot.abort_reason) {
    value.snapshot.updated_at = clock_();
  }
  value.snapshot.committed = control.committed;
  value.snapshot.abort_reason = control.abort_reason;
  if (control.abort_reason != AbortReason::None) value.snapshot.state = OperationState::Cancelling;
  else if (control.committed) value.snapshot.state = OperationState::Finalizing;
}

std::optional<OperationSnapshot> OperationRegistry::get(const TrustedAutomationContext& context, OperationId id) {
  checkThread();
  sweep();
  auto* value = record(context, id);
  return value ? std::optional<OperationSnapshot>{value->snapshot} : std::nullopt;
}

std::optional<OperationSnapshot> OperationRegistry::get(
    const TrustedAutomationContext& context, const OperationHandle& handle) {
  checkThread();
  sweep();
  if (!session(context) || !handle.valid() || handle.value.size() > limits_.max_opaque_handle_bytes) return std::nullopt;
  for (const auto& entry : records_) {
    if (sameConnection(entry.second.connection, context.connection) && entry.second.handle.value == handle.value) {
      return entry.second.snapshot;
    }
  }
  return std::nullopt;
}

std::optional<CancellationResult> OperationRegistry::cancel(
    const TrustedAutomationContext& context, OperationId id, AbortReason reason) {
  checkThread();
  sweep();
  auto* value = record(context, id);
  if (!value) return std::nullopt;
  const bool requested = value->control->requestCancel(reason);
  refresh(*value);
  return CancellationResult{OperationCancellation{id}, id, value->snapshot.state, requested};
}

bool OperationRegistry::advance(const TrustedAutomationContext& context, OperationId id,
    OperationState state, OperationProgress progress) {
  checkThread();
  auto* value = record(context, id);
  if (!value) return false;
  refresh(*value);
  auto remaining = limits_.max_frame_bytes;
  if (!transition(value->snapshot.state, state) ||
      !takeBytes(remaining, 64) || !takeBytes(remaining, progress.stage.capacity()) ||
      !takeBytes(remaining, progress.stop_reason.capacity())) return false;
  value->snapshot.state = state;
  value->snapshot.progress = std::move(progress);
  value->snapshot.updated_at = clock_();
  return true;
}

bool OperationRegistry::complete(const TrustedAutomationContext& context, OperationId id,
    ActionResult outcome, ActionResult* settled_outcome) {
  checkThread();
  auto* value = record(context, id, false);
  if (!value || isTerminal(value->snapshot.state)) return false;
  if (!boundedOutcome(outcome, limits_)) outcome = failure(ErrorCode::kResourceLimit, "operation metadata exceeds limit");
  if (const auto* captured = std::get_if<CapturedResult>(&outcome.output)) {
    if (outcome.ok && (captured->result_id == kInvalidResultId || captured->width <= 0 || captured->height <= 0)) {
      outcome = failure(ErrorCode::kInvalidArgument, "invalid captured result metadata");
    } else if (outcome.ok) {
      const auto result_id = captured->result_id;
      bool foreign = false;
      for (const auto& entry : sessions_) {
        if (entry.first != context.connection.generation && entry.second.result_id == result_id) foreign = true;
        for (const auto& invalid : entry.second.invalid_results) if (invalid.id == result_id) foreign = true;
      }
      if (foreign) outcome = failure(ErrorCode::kAccessDenied, "result is foreign or invalidated");
    }
  }
  auto settled = value->control->settle(std::move(outcome), value->requires_commit);
  if (!settled) return false;
  refresh(*value);
  settled->request_id = value->request_id;
  settled->operation_id = id;
  value->snapshot.state = settled->ok ? OperationState::Succeeded :
      settled->error_code == ErrorCode::kTimeout ? OperationState::TimedOut :
      settled->error_code == ErrorCode::kCancelled ? OperationState::Cancelled : OperationState::Failed;
  value->snapshot.outcome = std::move(*settled);
  value->snapshot.updated_at = clock_();
  value->snapshot.completed_at = value->snapshot.updated_at;
  const auto* captured = std::get_if<CapturedResult>(&value->snapshot.outcome->output);
  if (value->snapshot.outcome->ok && captured) {
    value->snapshot.result_availability = session(context) ? ResultAvailability::Available : ResultAvailability::Released;
    if (session(context)) (void)bindResult(context, captured->result_id);
  }
  if (settled_outcome) *settled_outcome = *value->snapshot.outcome;
  trim();
  return true;
}

ResultId OperationRegistry::currentResult(const TrustedAutomationContext& context) {
  checkThread();
  const auto* owner = session(context);
  return owner ? owner->result_id : kInvalidResultId;
}

std::optional<ResultHandle> OperationRegistry::bindResult(const TrustedAutomationContext& context, ResultId id) {
  checkThread();
  auto* owner = session(context);
  if (!owner || id == kInvalidResultId) return std::nullopt;
  if (owner->result_id == id) return owner->result_handle;
  for (const auto& entry : sessions_) {
    if (entry.first != context.connection.generation && entry.second.result_id == id) return std::nullopt;
    for (const auto& invalid : entry.second.invalid_results) if (invalid.id == id) return std::nullopt;
  }
  ResultHandle handle{newHandle()};
  if (owner->result_id != kInvalidResultId) invalidateResult(context, owner->result_id, ResultAvailability::Released);
  owner->result_id = id;
  owner->result_handle = std::move(handle);
  return owner->result_handle;
}

std::optional<ResultId> OperationRegistry::resolveResult(
    const TrustedAutomationContext& context, const ResultHandle& handle, bool include_invalidated) {
  checkThread();
  auto* owner = session(context);
  if (!owner || !handle.valid() || handle.value.size() > limits_.max_opaque_handle_bytes) return std::nullopt;
  if (owner->result_handle.value == handle.value) return owner->result_id;
  if (include_invalidated) for (const auto& invalid : owner->invalid_results) {
    if (invalid.handle.value == handle.value &&
        clock_() < deadline(invalid.invalidated_at, limits_.tombstone_ttl)) return invalid.id;
  }
  return std::nullopt;
}

void OperationRegistry::invalidateResult(const TrustedAutomationContext& context, ResultId id,
                                        ResultAvailability availability) {
  checkThread();
  auto* owner = session(context);
  if (!owner || id == kInvalidResultId ||
      (availability != ResultAvailability::Expired && availability != ResultAvailability::Released)) return;
  if (owner->result_id == id) {
    owner->invalid_results.push_back({id, owner->result_handle, clock_()});
    if (owner->invalid_results.size() > limits_.max_tombstones_per_connection) {
      owner->invalid_results.erase(owner->invalid_results.begin());
    }
    owner->result_id = kInvalidResultId;
    owner->result_handle = {};
  }
  for (auto& entry : records_) {
    auto& value = entry.second;
    if (!sameConnection(value.connection, context.connection) || !value.snapshot.outcome) continue;
    const auto* captured = std::get_if<CapturedResult>(&value.snapshot.outcome->output);
    if (captured && captured->result_id == id && value.snapshot.result_availability == ResultAvailability::Available) {
      value.snapshot.result_availability = availability;
      value.snapshot.updated_at = clock_();
    }
  }
}

void OperationRegistry::trim() {
  const auto now = clock_();
  for (auto it = records_.begin(); it != records_.end();) {
    if (it->second.snapshot.completed_at && now >= deadline(*it->second.snapshot.completed_at,
                                                           limits_.completed_operation_ttl)) it = records_.erase(it);
    else ++it;
  }
  for (auto session_it = sessions_.begin(); session_it != sessions_.end();) {
    auto& invalid_results = session_it->second.invalid_results;
    invalid_results.erase(std::remove_if(invalid_results.begin(), invalid_results.end(),
        [&](const Session::InvalidResult& value) {
          return now >= deadline(value.invalidated_at, limits_.tombstone_ttl);
        }), invalid_results.end());
    const auto generation = session_it->first;
    std::size_t terminal = 0;
    bool active = false;
    for (const auto& entry : records_) if (entry.second.connection.generation == generation) {
      if (isTerminal(entry.second.snapshot.state)) ++terminal;
      else active = true;
    }
    while (terminal > limits_.max_completed_operations_per_connection) {
      auto oldest = records_.end();
      for (auto it = records_.begin(); it != records_.end(); ++it) {
        if (it->second.connection.generation != generation || !it->second.snapshot.completed_at) continue;
        if (oldest == records_.end() || *it->second.snapshot.completed_at < *oldest->second.snapshot.completed_at ||
            (*it->second.snapshot.completed_at == *oldest->second.snapshot.completed_at && it->first < oldest->first)) oldest = it;
      }
      records_.erase(oldest);
      --terminal;
    }
    if (!session_it->second.connected && !active) {
      for (auto it = records_.begin(); it != records_.end();) {
        if (it->second.connection.generation == generation) it = records_.erase(it);
        else ++it;
      }
      session_it = sessions_.erase(session_it);
    } else ++session_it;
  }
}

void OperationRegistry::sweep() {
  checkThread();
  for (auto& entry : records_) refresh(entry.second);
  trim();
}

std::size_t OperationRegistry::recordCount() const { checkThread(); return records_.size(); }

}  // namespace qingying
