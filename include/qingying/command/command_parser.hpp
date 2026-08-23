#pragma once

#include "qingying/action/types.hpp"

#include <string>

namespace qingying {

class CommandParser {
 public:
  // Returns false if local table misses (caller may fallback to GUI / cloud).
  bool tryParse(const std::wstring& utterance, ActionRequest* out) const;
};

}  // namespace qingying
