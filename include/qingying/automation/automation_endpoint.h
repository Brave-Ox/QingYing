#pragma once
#include "qingying/automation/operation_coordinator.h"
namespace qingying {
// UI-thread transport facade; session state and execution live in the core.
class AutomationEndpoint final {
 public:
  using ExecutionPolicy = OperationCoordinator::ExecutionPolicy;
  AutomationEndpoint(AutomationOperationPort& operations, AutomationResultPort& results,
      OperationRegistry& registry, UiActionScheduler& scheduler, InteractionGate& gate,
      AutomationLimits limits, ExecutionPolicy policy);
  ~AutomationEndpoint();
  AutomationEndpoint(const AutomationEndpoint&) = delete;
  AutomationEndpoint& operator=(const AutomationEndpoint&) = delete;
  std::optional<TrustedAutomationContext> connectAuthenticated();
  void disconnect(const TrustedAutomationContext& context);
  void execute(UiMessageToken ticket, const TrustedAutomationContext& context,
      const AutomationRequest& request, std::shared_ptr<OperationControl> control);
  StatusInfo status() const;
  void setTransportEnabled(bool enabled);
  void tick();
  void beginShutdown();
  void stopBusinessProducers();
  void finishShutdown();
  void shutdown();
 private:
  OperationCoordinator coordinator_;
};
}  // namespace qingying
