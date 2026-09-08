#pragma once

#include "qingying/app/interaction_gate.h"
#include "qingying/automation/ui_action_scheduler.h"

namespace qingying {
class ActionDispatcher;
class CaptureWorkflow;
class ResultStore;

// Trusted UI-thread composition boundary. A transport must authenticate first,
// then ask the UI to connect; never construct scopes from client JSON. Submit
// through UiActionScheduler, whose Execute callback forwards to execute().
class AutomationEndpoint final {
 public:
  struct ExecutionPolicy {
    // Empty in production until the corresponding external handlers implement
    // scope, commit arbitration, limits and cancellation/cleanup contracts.
    std::vector<ActionType> ready_actions;
    // Stop/join all additional business producers before scheduler reclamation.
    std::function<void()> stop_producers;
    bool transport_enabled{false};
  };
  AutomationEndpoint(ActionDispatcher& dispatcher, CaptureWorkflow& workflow,
      ResultStore& results, OperationRegistry& registry,
      UiActionScheduler& scheduler, InteractionGate& gate,
      AutomationLimits limits, ExecutionPolicy policy);
  ~AutomationEndpoint();
  std::optional<TrustedAutomationContext> connectAuthenticated();
  void disconnect(const TrustedAutomationContext& context);
  void execute(UiMessageToken ticket, const TrustedAutomationContext& context,
      const AutomationRequest& request, std::shared_ptr<OperationControl> control);
  StatusInfo status() const;
  void setTransportEnabled(bool enabled);
  void tick();
  void shutdown();

 private:
  struct Execution {
    TrustedAutomationContext context;
    OperationId operation_id;
    OperationHandle operation_handle;
    RequestId request_id;
    InteractionGate::Guard guard;
    std::optional<ActionResult> outcome;
    std::vector<std::pair<UiMessageToken, RequestId>> waiters;
  };
  void checkThread() const;
  bool connected(const TrustedAutomationContext& context) const;
  void settle(UiMessageToken ticket, AutomationResponse& response);
  ActionDispatcher& dispatcher_;
  CaptureWorkflow& workflow_;
  ResultStore& results_;
  OperationRegistry& registry_;
  UiActionScheduler& scheduler_;
  InteractionGate& gate_;
  AutomationLimits limits_;
  ExecutionPolicy policy_;
  std::thread::id ui_thread_{std::this_thread::get_id()};
  bool stopping_{false};
  ResultScopeId next_scope_{kGuiResultScopeId + 1};
  std::map<ConnectionGeneration, TrustedAutomationContext> connections_;
  std::map<UiMessageToken, std::shared_ptr<Execution>> executions_;
};
}  // namespace qingying
