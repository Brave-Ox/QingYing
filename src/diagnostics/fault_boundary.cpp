#include "qingying/diagnostics/fault_boundary.h"
#include <Windows.h>
#include <atomic>
#include <cstdio>
#include <mutex>

namespace qingying {
namespace {
thread_local FaultContext active_context;
std::atomic<std::uint64_t> sequence{0};
struct Records {
  std::mutex mutex;
  std::array<FaultDiagnostic, 256> faults{};
  std::array<std::uint64_t, 6> counts{};
  std::size_t next{0}, size{0};
};
Records& records() { static Records value; return value; }
void token(const char* source, std::array<char, 64>& target) noexcept {
  if (!source) return;
  for (std::size_t i = 0; i + 1 < target.size() && source[i]; ++i) {
    const auto c = source[i];
    target[i] = ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_') ? c : '_';
  }
}
}
const char* faultOriginName(FaultOrigin value) noexcept {
  switch (value) {
    case FaultOrigin::Ui: return "ui";
    case FaultOrigin::Scheduler: return "scheduler";
    case FaultOrigin::Pipe: return "pipe";
    case FaultOrigin::Handler: return "handler";
    case FaultOrigin::Plugin: return "plugin";
    case FaultOrigin::Worker: return "worker";
  }
  return "unknown";
}
const char* faultDomainName(FaultDomain value) noexcept {
  switch (value) {
    case FaultDomain::Request: return "request";
    case FaultDomain::Session: return "session";
    case FaultDomain::Provider: return "provider";
    case FaultDomain::Application: return "application";
  }
  return "unknown";
}
FaultContext currentFaultContext() noexcept { return active_context; }
DiagnosticScope::DiagnosticScope(FaultContext context) noexcept : previous_(active_context) {
  if (!context.epoch) context.epoch = previous_.epoch;
  if (!context.session_id) context.session_id = previous_.session_id;
  if (!context.scope_id) context.scope_id = previous_.scope_id;
  if (!context.request_id) context.request_id = previous_.request_id;
  if (!context.operation_id) context.operation_id = previous_.operation_id;
  if (context.started_at == std::chrono::steady_clock::time_point{}) context.started_at = previous_.started_at;
  active_context = context;
}
DiagnosticScope::~DiagnosticScope() { active_context = previous_; }
FaultDiagnostic recordFault(int code, FaultOrigin origin, FaultDomain domain,
                            const char* provider, FaultContext context) noexcept {
  FaultDiagnostic fault;
  fault.error_code = code; fault.origin = origin; fault.domain = domain; fault.context = context;
  fault.retryable = code == ErrorCode::kBusy || code == ErrorCode::kNotReady || code == ErrorCode::kTimeout;
  const auto now = std::chrono::steady_clock::now();
  if (context.started_at != std::chrono::steady_clock::time_point{} && now > context.started_at)
    fault.elapsed_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now - context.started_at).count());
  token(provider, fault.provider_id);
  if (context.request_id) std::snprintf(fault.correlation_id.data(), fault.correlation_id.size(),
      "a%llu-s%llu-r%llu", context.epoch, context.session_id, context.request_id);
  else std::snprintf(fault.correlation_id.data(), fault.correlation_id.size(), "local-%llu", ++sequence);
  try {
    auto& store = records();
    std::uint64_t count = 0;
    {
      std::lock_guard<std::mutex> lock(store.mutex);
      store.faults[store.next] = fault;
      store.next = (store.next + 1) % store.faults.size();
      if (store.size < store.faults.size()) ++store.size;
      const auto index = static_cast<unsigned>(origin);
      if (index < store.counts.size()) count = ++store.counts[index];
    }
    if (count <= 8 || (count & (count - 1)) == 0) {
      char line[512]{};
      std::snprintf(line, sizeof(line),
          "QingYing fault correlation=%s code=%s origin=%s domain=%s retryable=%d request=%llu session=%llu scope=%llu operation=%llu provider=%s elapsed_ms=%llu sample=%llu\n",
          fault.correlation_id.data(), errorCodeSymbol(code).data(), faultOriginName(origin), faultDomainName(domain),
          fault.retryable, context.request_id, context.session_id, context.scope_id, context.operation_id,
          fault.provider_id.data(), fault.elapsed_ms, count);
      OutputDebugStringA(line);
    }
  } catch (...) { /* Diagnostic recording cannot become a second failure. */ }
  return fault;
}
std::vector<FaultDiagnostic> recentFaults() {
  auto& store = records(); std::lock_guard<std::mutex> lock(store.mutex);
  std::vector<FaultDiagnostic> result; result.reserve(store.size);
  for (std::size_t i = 0; i < store.size; ++i)
    result.push_back(store.faults[(store.next + store.faults.size() - store.size + i) % store.faults.size()]);
  return result;
}
}  // namespace qingying
