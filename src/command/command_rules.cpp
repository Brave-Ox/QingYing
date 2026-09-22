#include "command_rules.hpp"

#include "command_dimension_parser.hpp"
#include "command_text.hpp"

#include <array>
#include <string_view>
#include <utility>

namespace qingying::command_detail {
namespace {

bool parseSave(const std::wstring& utterance, ActionRequest* out) {
  std::wstring path;
  if (startsWith(utterance, L"保存到") || startsWith(utterance, L"保存为")) {
    path = utterance.substr(3);
  } else if (startsWith(utterance, L"保存")) {
    path = utterance.substr(2);
  } else {
    return false;
  }
  path = stripQuotes(stripLeadingPunctuation(std::move(path)));
  if (path.empty()) return false;
  *out = makeActionRequest(SaveRequest{ResultSelection::current(), std::move(path)});
  return true;
}

enum class WindowFollowUp {
  None,
  Copy,
  Save,
  Pin,
};

struct WindowCaptureParseResult {
  std::wstring query;
  WindowFollowUp follow_up{WindowFollowUp::None};
};

struct ExactRule {
  std::wstring_view utterance;
  RuleMatch match;
};

constexpr std::array<ExactRule, 6> kExactRules = {{
    {L"复制", RuleMatch::Copy},
    {L"复制当前截图", RuleMatch::Copy},
    {L"钉图", RuleMatch::Pin},
    {L"钉住当前截图", RuleMatch::Pin},
    {L"状态", RuleMatch::Status},
    {L"查看状态", RuleMatch::Status},
}};

bool parseExactRule(const std::wstring& command, RuleParseResult* out) {
  for (const ExactRule& rule : kExactRules) {
    if (command != rule.utterance) continue;

    RuleParseResult result;
    result.match = rule.match;
    switch (rule.match) {
      case RuleMatch::Copy:
        result.primary = makeActionRequest(CopyRequest{ResultSelection::current()});
        break;
      case RuleMatch::Pin:
        result.primary = makeActionRequest(PinRequest{ResultSelection::current()});
        break;
      case RuleMatch::Status:
        result.primary = makeActionRequest(StatusRequest{});
        break;
      case RuleMatch::None:
      case RuleMatch::Save:
      case RuleMatch::CropCenter:
      case RuleMatch::CaptureWindow:
        return false;
    }
    *out = std::move(result);
    return true;
  }
  return false;
}

WindowFollowUp trimWindowFollowUp(std::wstring* utterance) {
  if (utterance == nullptr) return WindowFollowUp::None;
  constexpr std::array<std::pair<std::wstring_view, WindowFollowUp>, 3>
      kFollowUps = {{
          {L"并复制", WindowFollowUp::Copy},
          {L"并保存", WindowFollowUp::Save},
          {L"并钉图", WindowFollowUp::Pin},
      }};
  for (const auto& candidate : kFollowUps) {
    if (!endsWith(*utterance, candidate.first)) continue;
    utterance->resize(utterance->size() - candidate.first.size());
    *utterance = trim(std::move(*utterance));
    return candidate.second;
  }
  return WindowFollowUp::None;
}

bool parseWindowCapture(std::wstring utterance, WindowCaptureParseResult* out) {
  if (out == nullptr) return false;
  const WindowFollowUp follow_up = trimWindowFollowUp(&utterance);
  std::wstring query;
  for (const std::wstring_view prefix : {L"截取窗口", L"截图窗口", L"截屏窗口"}) {
    if (startsWith(utterance, prefix)) {
      query = stripLeadingPunctuation(utterance.substr(prefix.size()));
      break;
    }
  }
  if (query.empty() &&
      (startsWith(utterance, L"截取") || startsWith(utterance, L"截图")) &&
      endsWith(utterance, L"窗口")) {
    query = trim(utterance.substr(2, utterance.size() - 4));
    if (startsWith(query, L"名为") && endsWith(query, L"的")) {
      query = query.substr(2, query.size() - 3);
    }
  }
  query = stripQuotes(trim(std::move(query)));
  if (query.empty()) return false;
  *out = {std::move(query), follow_up};
  return true;
}

bool makeWindowFollowUp(WindowFollowUp follow_up, ActionRequest* out) {
  if (out == nullptr) return false;
  switch (follow_up) {
    case WindowFollowUp::Copy:
      *out = makeActionRequest(CopyRequest{ResultSelection::current()});
      return true;
    case WindowFollowUp::Pin:
      *out = makeActionRequest(PinRequest{ResultSelection::current()});
      return true;
    case WindowFollowUp::None:
    case WindowFollowUp::Save:
      return false;
  }
  return false;
}

}  // namespace

bool parseRule(const std::wstring& command, RuleParseResult* out) {
  if (out == nullptr) return false;
  RuleParseResult result;
  if (parseExactRule(command, &result)) {
    // The fixed aliases share one table so supported command text cannot
    // drift from the rule name used by diagnostics.
  } else if (parseSave(command, &result.primary)) {
    result.match = RuleMatch::Save;
  } else {
    Dimensions dimensions;
    if (parseCenteredCropDimensions(command, &dimensions)) {
      result.primary = makeActionRequest(
          CropCenterRequest{dimensions.width, dimensions.height});
      result.match = RuleMatch::CropCenter;
    } else {
      WindowCaptureParseResult window_capture;
      if (!parseWindowCapture(command, &window_capture)) return false;
      result.match = RuleMatch::CaptureWindow;
      result.primary = makeActionRequest(
          CaptureWindowRequest{std::move(window_capture.query)});
      result.has_follow_up =
          makeWindowFollowUp(window_capture.follow_up, &result.follow_up);
    }
  }
  *out = std::move(result);
  return true;
}

const wchar_t* ruleMatchName(RuleMatch match) noexcept {
  switch (match) {
    case RuleMatch::Copy: return L"copy";
    case RuleMatch::Pin: return L"pin";
    case RuleMatch::Status: return L"status";
    case RuleMatch::Save: return L"save";
    case RuleMatch::CropCenter: return L"crop-center";
    case RuleMatch::CaptureWindow: return L"capture-window";
    case RuleMatch::None: break;
  }
  return L"none";
}

}  // namespace qingying::command_detail
