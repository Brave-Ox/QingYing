#pragma once

#include "qingying/command/command_parser.hpp"

#include <cstddef>

namespace qingying {

enum class CommandPlanValidationCode {
  Ok,
  Empty,
  TooManyActions,
  InvalidFollowUp,
};

struct CommandPlanValidationResult {
  CommandPlanValidationCode code{CommandPlanValidationCode::Ok};
  std::size_t action_index{0};

  explicit operator bool() const noexcept {
    return code == CommandPlanValidationCode::Ok;
  }
};

// A passive command-layer invariant checker. App execution does not depend on
// it; the parser uses it only to protect the plan it has already constructed.
CommandPlanValidationResult validateCommandPlan(const CommandPlan& plan) noexcept;

}  // namespace qingying
