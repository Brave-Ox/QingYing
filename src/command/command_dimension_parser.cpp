#include "command_dimension_parser.hpp"

#include <cwctype>
#include <limits>

namespace qingying::command_detail {
namespace {

bool parsePositiveInt(const std::wstring& text, std::size_t* cursor,
                      int* value) {
  if (cursor == nullptr || value == nullptr || *cursor >= text.size() ||
      !std::iswdigit(text[*cursor])) {
    return false;
  }
  unsigned long long parsed = 0;
  while (*cursor < text.size() && std::iswdigit(text[*cursor])) {
    parsed = parsed * 10u + static_cast<unsigned>(text[*cursor] - L'0');
    if (parsed > static_cast<unsigned long long>((std::numeric_limits<int>::max)())) {
      return false;
    }
    ++*cursor;
  }
  if (parsed == 0) return false;
  *value = static_cast<int>(parsed);
  return true;
}

bool hasCropIntent(const std::wstring& utterance) {
  return utterance.find(L"中心") != std::wstring::npos ||
         utterance.find(L"中间") != std::wstring::npos ||
         utterance.find(L"居中") != std::wstring::npos ||
         utterance.find(L"裁剪") != std::wstring::npos ||
         utterance.find(L"裁切") != std::wstring::npos;
}

}  // namespace

bool parseCenteredCropDimensions(const std::wstring& utterance,
                                 Dimensions* dimensions) {
  if (dimensions == nullptr || !hasCropIntent(utterance)) return false;
  for (std::size_t index = 0; index < utterance.size(); ++index) {
    if (!std::iswdigit(utterance[index])) continue;
    std::size_t cursor = index;
    Dimensions parsed;
    if (!parsePositiveInt(utterance, &cursor, &parsed.width)) continue;
    while (cursor < utterance.size() && std::iswspace(utterance[cursor])) ++cursor;
    if (cursor >= utterance.size() ||
        (utterance[cursor] != L'x' && utterance[cursor] != L'X' &&
         utterance[cursor] != L'×' && utterance[cursor] != L'*')) {
      continue;
    }
    ++cursor;
    while (cursor < utterance.size() && std::iswspace(utterance[cursor])) ++cursor;
    if (!parsePositiveInt(utterance, &cursor, &parsed.height)) continue;
    *dimensions = parsed;
    return true;
  }
  return false;
}

}  // namespace qingying::command_detail
