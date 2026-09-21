#include "command_text.hpp"

#include <algorithm>
#include <cwctype>
#include <utility>

namespace qingying::command_detail {

std::wstring trim(std::wstring value) {
  const auto not_space = [](wchar_t value) { return !std::iswspace(value); };
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
  std::size_t first = 0;
  while (first < value.size() &&
         (value[first] == L':' || value[first] == L'：' ||
          value[first] == L'，' || value[first] == L',' ||
          std::iswspace(value[first]))) {
    ++first;
  }
  return trim(value.substr(first));
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

std::wstring normalize(std::wstring value) {
  // Compatibility normalization intentionally stays limited to the existing
  // parser behavior: leading and trailing whitespace only.
  return trim(std::move(value));
}

}  // namespace qingying::command_detail
