#pragma once

#include <cstddef>
#include <string>

namespace qingying {

// Parsing remains fully local. The diagnostic is optional and never changes
// the compatibility result returned by CommandParser.
enum class CommandParseStage {
  None,
  Normalize,
  MatchRule,
  ValidatePlan,
  Complete,
};

struct CommandParseDiagnostic {
  CommandParseStage stage{CommandParseStage::None};
  std::wstring matched_rule;
  std::wstring normalized_input;
  std::wstring message;
  std::size_t input_length{0};
  bool succeeded{false};
};

const wchar_t* commandParseStageName(CommandParseStage stage) noexcept;

}  // namespace qingying
