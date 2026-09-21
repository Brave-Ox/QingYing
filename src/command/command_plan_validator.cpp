#include "qingying/command/command_plan_validator.hpp"

namespace qingying {

CommandPlanValidationResult validateCommandPlan(const CommandPlan& plan) noexcept {
  if (plan.actions.empty()) return {CommandPlanValidationCode::Empty, 0};
  if (plan.actions.size() > 2) {
    return {CommandPlanValidationCode::TooManyActions, 2};
  }
  if (plan.actions.size() == 2) {
    const ActionType first = plan.actions.front().type();
    const ActionType follow_up = plan.actions.back().type();
    if (first != ActionType::CaptureWindow ||
        (follow_up != ActionType::Copy && follow_up != ActionType::Pin)) {
      return {CommandPlanValidationCode::InvalidFollowUp, 1};
    }
  }
  return {};
}

}  // namespace qingying
