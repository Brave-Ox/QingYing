#include "qingying/automation/automation_endpoint.h"
namespace qingying {
AutomationEndpoint::AutomationEndpoint(AutomationOperationPort& operations, AutomationResultPort& results,
    OperationRegistry& registry, UiActionScheduler& scheduler, InteractionGate& gate,
    AutomationLimits limits, ExecutionPolicy policy)
    : coordinator_(operations, results, registry, scheduler, gate, limits, std::move(policy)) {}
AutomationEndpoint::~AutomationEndpoint() = default;
std::optional<TrustedAutomationContext> AutomationEndpoint::connectAuthenticated() { return coordinator_.connectAuthenticated(); }
void AutomationEndpoint::disconnect(const TrustedAutomationContext& context) { coordinator_.disconnect(context); }
void AutomationEndpoint::execute(UiMessageToken ticket, const TrustedAutomationContext& context,
    const AutomationRequest& request, std::shared_ptr<OperationControl> control) {
  coordinator_.execute(ticket, context, request, std::move(control));
}
StatusInfo AutomationEndpoint::status() const { return coordinator_.status(); }
void AutomationEndpoint::setTransportEnabled(bool enabled) { coordinator_.setTransportEnabled(enabled); }
void AutomationEndpoint::tick() { coordinator_.tick(); }
void AutomationEndpoint::beginShutdown() { coordinator_.beginShutdown(); }
void AutomationEndpoint::stopBusinessProducers() { coordinator_.stopBusinessProducers(); }
void AutomationEndpoint::finishShutdown() { coordinator_.finishShutdown(); }
void AutomationEndpoint::shutdown() { coordinator_.shutdown(); }
}  // namespace qingying
