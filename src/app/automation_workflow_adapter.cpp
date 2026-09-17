#include "qingying/app/automation_workflow_adapter.h"
#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/capture_workflow.hpp"
#include "qingying/app/result_store.h"
#include <stdexcept>

namespace qingying {
AutomationWorkflowAdapter::AutomationWorkflowAdapter(ActionDispatcher& dispatcher,
    CaptureWorkflow& workflow, ResultStore& results,
    std::vector<AutomationActionDescriptor> descriptors)
    : dispatcher_(dispatcher), workflow_(workflow), results_(results),
      descriptors_(std::move(descriptors)) {
  for (std::size_t i = 0; i < descriptors_.size(); ++i) {
    if (descriptors_[i].capability.empty()) throw std::invalid_argument("empty capability");
    for (std::size_t j = 0; j < i; ++j)
      if (descriptors_[i].type == descriptors_[j].type)
        throw std::invalid_argument("duplicate action descriptor");
  }
}
std::optional<AutomationActionDescriptor> AutomationWorkflowAdapter::describe(ActionType type) const {
  for (const auto& descriptor : descriptors_)
    if (descriptor.type == type && dispatcher_.hasHandler(type)) return descriptor;
  return std::nullopt;
}
void AutomationWorkflowAdapter::submit(const ActionRequest& request, ActionCompletion completion) {
  dispatcher_.submit(request, std::move(completion));
}
void AutomationWorkflowAdapter::shutdown() { workflow_.shutdown(); }
AutomationResultUsage AutomationWorkflowAdapter::budgetSnapshot() const noexcept {
  const auto usage = results_.budgetSnapshot();
  return {usage.retained_bytes, usage.reserved_bytes};
}
int AutomationWorkflowAdapter::resultStatus(ResultScopeId scope, ResultId id) const noexcept {
  return results_.resultStatus(scope, id);
}
int AutomationWorkflowAdapter::releaseResult(ResultScopeId scope, ResultId id) noexcept {
  return results_.releaseResult(scope, id);
}
void AutomationWorkflowAdapter::clearScope(ResultScopeId scope) noexcept { results_.clearScope(scope); }
void AutomationWorkflowAdapter::clearAll() noexcept { results_.clearAll(); }
void AutomationWorkflowAdapter::sweep() noexcept { results_.sweep(); }
}  // namespace qingying
