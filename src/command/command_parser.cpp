#include "qingying/command/command_parser.hpp"

#include <algorithm>
#include <cwctype>
#include <limits>
#include <string_view>

namespace qingying {
namespace {

std::wstring trim(std::wstring value) {
  const auto not_space = [](wchar_t c) { return !std::iswspace(c); };
  const auto first = std::find_if(value.begin(), value.end(), not_space);
  if (first == value.end()) return {};
  const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
  return {first, last};
}

bool startsWith(const std::wstring& value, std::wstring_view prefix) {
  return value.size() >= prefix.size() &&
         value.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::wstring& value, std::wstring_view suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::wstring stripLeadingPunctuation(std::wstring value) {
  while (!value.empty() && (value.front() == L':' || value.front() == L'：' ||
                            value.front() == L'，' || value.front() == L',' ||
                            std::iswspace(value.front()))) {
    value.erase(value.begin());
  }
  return trim(std::move(value));
}

std::wstring stripQuotes(std::wstring value) {
  value = trim(std::move(value));
  if (value.size() >= 2 &&
      ((value.front() == L'"' && value.back() == L'"') ||
       (value.front() == L'“' && value.back() == L'”') ||
       (value.front() == L'\'' && value.back() == L'\''))) {
    value = value.substr(1, value.size() - 2);
  }
  return trim(std::move(value));
}

bool parsePositiveInt(const std::wstring& text, std::size_t* cursor, int* value) {
  if (cursor == nullptr || value == nullptr || *cursor >= text.size() ||
      !std::iswdigit(text[*cursor])) return false;

  unsigned long long parsed = 0;
  while (*cursor < text.size() && std::iswdigit(text[*cursor])) {
    parsed = parsed * 10u + static_cast<unsigned>(text[*cursor] - L'0');
    if (parsed > static_cast<unsigned long long>((std::numeric_limits<int>::max)()))
      return false;
    ++*cursor;
  }
  if (parsed == 0) return false;
  *value = static_cast<int>(parsed);
  return true;
}

bool parseCropCenter(const std::wstring& utterance, ActionRequest* out) {
  const bool has_crop_intent = utterance.find(L"中心") != std::wstring::npos ||
                               utterance.find(L"中间") != std::wstring::npos ||
                               utterance.find(L"居中") != std::wstring::npos ||
                               utterance.find(L"裁剪") != std::wstring::npos ||
                               utterance.find(L"裁切") != std::wstring::npos;
  if (!has_crop_intent) return false;

  for (std::size_t index = 0; index < utterance.size(); ++index) {
    if (!std::iswdigit(utterance[index])) continue;
    std::size_t cursor = index;
    int width = 0;
    int height = 0;
    if (!parsePositiveInt(utterance, &cursor, &width)) continue;
    while (cursor < utterance.size() && std::iswspace(utterance[cursor])) ++cursor;
    if (cursor >= utterance.size() ||
        (utterance[cursor] != L'x' && utterance[cursor] != L'X' &&
         utterance[cursor] != L'×' && utterance[cursor] != L'*')) continue;
    ++cursor;
    while (cursor < utterance.size() && std::iswspace(utterance[cursor])) ++cursor;
    if (!parsePositiveInt(utterance, &cursor, &height)) continue;
    *out = makeActionRequest(CropCenterRequest{width, height});
    return true;
  }
  return false;
}

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
  if (query.empty() && (startsWith(utterance, L"截取") || startsWith(utterance, L"截图")) &&
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

bool CommandParser::tryParse(const std::wstring& utterance,
                             ActionRequest* out) const {
  if (out == nullptr) return false;

  const std::wstring command = trim(utterance);
  if (command.empty()) return false;

  ActionRequest parsed;
  if (command == L"复制" || command == L"复制当前截图") {
    parsed = makeActionRequest(CopyRequest{ResultSelection::current()});
  } else if (command == L"钉图" || command == L"钉住当前截图") {
    parsed = makeActionRequest(PinRequest{ResultSelection::current()});
  } else if (command == L"状态" || command == L"查看状态") {
    parsed = makeActionRequest(StatusRequest{});
  } else if (parseSave(command, &parsed) || parseCropCenter(command, &parsed) ||
             parseWindowCapture(command, &parsed)) {
    // The helper has already produced a typed request.
  } else {
    return false;
  }

  *out = std::move(parsed);
  return true;
}

}  // namespace qingying
