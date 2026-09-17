#pragma once

#include "qingying/automation/action_catalog.h"
#include <vector>

namespace qingying {
class ActionDispatcher;
class CaptureWorkflow;
class ResultStore;

class AutomationWorkflowAdapter final : public AutomationOperationPort,
                                        public AutomationResultPort {
 public:
  AutomationWorkflowAdapter(ActionDispatcher& dispatcher, CaptureWorkflow& workflow,
      ResultStore& results, std::vector<AutomationActionDescriptor> descriptors = automationActionDescriptors());
  std::optional<AutomationActionDescriptor> describe(ActionType type) const override;
  void submit(const ActionRequest& request, ActionCompletion completion) override;
  void shutdown() override;
  AutomationResultUsage budgetSnapshot() const noexcept override;
  int resultStatus(ResultScopeId scope, ResultId id) const noexcept override;
  int releaseResult(ResultScopeId scope, ResultId id) noexcept override;
  void clearScope(ResultScopeId scope) noexcept override;
  void clearAll() noexcept override;
  void sweep() noexcept override;

 private:
  ActionDispatcher& dispatcher_;
  CaptureWorkflow& workflow_;
  ResultStore& results_;
  std::vector<AutomationActionDescriptor> descriptors_;
};
}  // namespace qingying
