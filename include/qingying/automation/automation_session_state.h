#pragma once
#include "qingying/automation/automation_contract.h"
#include <map>
#include <set>
#include <stdexcept>
namespace qingying {
enum class AutomationSessionPhase { Active, Closing, Closed };
enum class AutomationSessionCloseReason { None, Disconnected, Shutdown, AdmissionRejected };
struct AutomationPeerIdentity {
  // Authentication is supplied by the trusted transport, never wire JSON.
  // A PID is optional: do not manufacture one when the transport did not pass it.
  std::optional<std::uint32_t> process_id;
};
// UI-thread session value. Owns only identities/operation IDs/result handles;
// images, memory budgets, interaction guards and producers remain application-owned.
class AutomationSessionState {
 public:
  explicit AutomationSessionState(TrustedAutomationContext context, AutomationPeerIdentity peer = {})
      : scope(context.action.result_scope), context_(std::move(context)), peer_(std::move(peer)) {
    if (!context_.valid() || scope == kGuiResultScopeId) throw std::invalid_argument("invalid automation session");
  }
  bool accepts(const TrustedAutomationContext& context) const {
    return connected && context.valid() && sameConnection(context_.connection, context.connection) &&
        scope == context.action.result_scope;
  }
  bool close(AutomationSessionCloseReason reason) {
    if (!connected || reason == AutomationSessionCloseReason::None) return false;
    connected = false;
    close_reason_ = reason;
    phase_ = pending_operations_.empty() ? AutomationSessionPhase::Closed : AutomationSessionPhase::Closing;
    result_id = kInvalidResultId;
    result_handle = {};
    invalid_results.clear();
    idempotency_entries_.clear();
    return true;
  }
  const TrustedAutomationContext& context() const { return context_; }
  const AutomationPeerIdentity& peer() const { return peer_; }
  AutomationSessionPhase phase() const { return phase_; }
  AutomationSessionCloseReason closeReason() const { return close_reason_; }
  std::size_t pendingOperationCount() const { return pending_operations_.size(); }
  std::size_t idempotencyEntryCount() const { return idempotency_entries_.size(); }
  std::optional<ResultHandle> ownedResultHandle() const {
    return result_handle.valid() ? std::optional<ResultHandle>{result_handle} : std::nullopt;
  }
 private:
  friend class OperationRegistry;
  struct InvalidResult {
    ResultId id;
    ResultHandle handle;
    std::chrono::steady_clock::time_point invalidated_at;
  };
  void admitted(OperationId id, const std::optional<std::string>& key) {
    pending_operations_.insert(id);
    try { if (key) idempotency_entries_.emplace(*key, id); }
    catch (...) { pending_operations_.erase(id); throw; }
  }
  void settled(OperationId id) {
    pending_operations_.erase(id);
    if (!connected && pending_operations_.empty()) phase_ = AutomationSessionPhase::Closed;
  }
  ResultScopeId scope;
  bool connected{true};
  ResultId result_id{kInvalidResultId};
  ResultHandle result_handle;
  std::vector<InvalidResult> invalid_results;
  TrustedAutomationContext context_;
  AutomationPeerIdentity peer_;
  AutomationSessionPhase phase_{AutomationSessionPhase::Active};
  AutomationSessionCloseReason close_reason_{AutomationSessionCloseReason::None};
  std::set<OperationId> pending_operations_;
  std::map<std::string, OperationId> idempotency_entries_;
};
}  // namespace qingying
