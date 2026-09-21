#include "command_rules.hpp"

#include "command_dimension_parser.hpp"
#include "command_text.hpp"

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

bool parseWindowCapture(std::wstring utterance, ActionRequest* out) {
  for (const std::wstring_view suffix : {L"并复制", L"并保存", L"并钉图"}) {
    if (endsWith(utterance, suffix)) {
      utterance.resize(utterance.size() - suffix.size());
      utterance = trim(std::move(utterance));
      break;
    }
  }
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
  *out = makeActionRequest(CaptureWindowRequest{std::move(query)});
  return true;
}

}  // namespace

bool parseRule(const std::wstring& command, RuleParseResult* out) {
  if (out == nullptr) return false;
  RuleParseResult result;
  if (command == L"复制" || command == L"复制当前截图") {
    result.primary = makeActionRequest(CopyRequest{ResultSelection::current()});
    result.match = RuleMatch::Copy;
  } else if (command == L"钉图" || command == L"钉住当前截图") {
    result.primary = makeActionRequest(PinRequest{ResultSelection::current()});
    result.match = RuleMatch::Pin;
  } else if (command == L"状态" || command == L"查看状态") {
    result.primary = makeActionRequest(StatusRequest{});
    result.match = RuleMatch::Status;
  } else if (parseSave(command, &result.primary)) {
    result.match = RuleMatch::Save;
  } else {
    Dimensions dimensions;
    if (parseCenteredCropDimensions(command, &dimensions)) {
      result.primary = makeActionRequest(
          CropCenterRequest{dimensions.width, dimensions.height});
      result.match = RuleMatch::CropCenter;
    } else if (parseWindowCapture(command, &result.primary)) {
      result.match = RuleMatch::CaptureWindow;
      if (endsWith(command, L"并复制")) {
        result.follow_up = makeActionRequest(CopyRequest{ResultSelection::current()});
        result.has_follow_up = true;
      } else if (endsWith(command, L"并钉图")) {
        result.follow_up = makeActionRequest(PinRequest{ResultSelection::current()});
        result.has_follow_up = true;
      }
    } else {
      return false;
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
