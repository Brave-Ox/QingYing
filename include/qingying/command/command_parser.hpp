#pragma once

#include "qingying/action/action_request.hpp"

#include <string>
#include <vector>

namespace qingying {

struct CommandPlan {
  std::vector<ActionRequest> actions;

  bool empty() const noexcept { return actions.empty(); }
};

class CommandParser {
 public:
  // A capture followed by “并复制” or “并钉图” becomes two ordered actions.
  bool tryParsePlan(const std::wstring& utterance, CommandPlan* out) const;

  // Returns false if local table misses (caller may fallback to GUI / cloud).
  bool tryParse(const std::wstring& utterance, ActionRequest* out) const;
};

}  // namespace qingying
