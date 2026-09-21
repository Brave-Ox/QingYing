#include "qingying/command/command_parser.hpp"

#include "qingying/command/command_plan_validator.hpp"
#include "command_rules.hpp"
#include "command_text.hpp"

#include <utility>

namespace qingying {

bool CommandParser::tryParse(const std::wstring& utterance,
                             ActionRequest* out) const {
  if (out == nullptr) return false;
  CommandPlan plan;
  if (!tryParsePlan(utterance, &plan) || plan.empty()) return false;
  *out = std::move(plan.actions.front());
  return true;
}

bool CommandParser::tryParsePlan(const std::wstring& utterance,
                                 CommandPlan* out) const {
  return tryParsePlan(utterance, out, nullptr);
}

bool CommandParser::tryParsePlan(const std::wstring& utterance,
                                 CommandPlan* out,
                                 CommandParseDiagnostic* diagnostic) const {
  if (diagnostic != nullptr) {
    *diagnostic = CommandParseDiagnostic{};
    diagnostic->stage = CommandParseStage::Normalize;
    diagnostic->input_length = utterance.size();
  }
  if (out == nullptr) {
    if (diagnostic != nullptr) diagnostic->message = L"output is null";
    return false;
  }

  const std::wstring command = command_detail::normalize(utterance);
  if (diagnostic != nullptr) diagnostic->normalized_input = command;
  if (command.empty()) {
    if (diagnostic != nullptr) diagnostic->message = L"input is empty";
    return false;
  }

  command_detail::RuleParseResult parsed;
  if (diagnostic != nullptr) diagnostic->stage = CommandParseStage::MatchRule;
  if (!command_detail::parseRule(command, &parsed)) {
    if (diagnostic != nullptr) diagnostic->message = L"no compatible rule";
    return false;
  }

  CommandPlan plan;
  plan.actions.push_back(std::move(parsed.primary));
  if (parsed.has_follow_up) plan.actions.push_back(std::move(parsed.follow_up));
  if (diagnostic != nullptr) {
    diagnostic->matched_rule = command_detail::ruleMatchName(parsed.match);
    diagnostic->stage = CommandParseStage::ValidatePlan;
  }
  if (!validateCommandPlan(plan)) {
    if (diagnostic != nullptr) diagnostic->message = L"invalid command plan";
    return false;
  }

  *out = std::move(plan);
  if (diagnostic != nullptr) {
    diagnostic->stage = CommandParseStage::Complete;
    diagnostic->succeeded = true;
  }
  return true;
}

}  // namespace qingying
