#pragma once

#include "qingying/action/action_request.hpp"

#include <string>

namespace qingying {

class CommandParser {
 public:
  // Returns false if local table misses (caller may fallback to GUI / cloud).
  bool tryParse(const std::wstring& utterance, ActionRequest* out) const;
};

}  // namespace qingying
