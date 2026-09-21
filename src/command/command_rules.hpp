#pragma once

#include "qingying/action/action_request.hpp"

#include <string>

namespace qingying::command_detail {

enum class RuleMatch {
  None,
  Copy,
  Pin,
  Status,
  Save,
  CropCenter,
  CaptureWindow,
};

struct RuleParseResult {
  ActionRequest primary;
  ActionRequest follow_up;
  RuleMatch match{RuleMatch::None};
  bool has_follow_up{false};
};

bool parseRule(const std::wstring& command, RuleParseResult* out);
const wchar_t* ruleMatchName(RuleMatch match) noexcept;

}  // namespace qingying::command_detail
