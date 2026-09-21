#pragma once

#include <string>
#include <string_view>

namespace qingying::command_detail {

std::wstring trim(std::wstring value);
bool startsWith(const std::wstring& value, std::wstring_view prefix);
bool endsWith(const std::wstring& value, std::wstring_view suffix);
std::wstring stripLeadingPunctuation(std::wstring value);
std::wstring stripQuotes(std::wstring value);

// Normalizes only presentation differences already accepted by the parser.
// It deliberately does not add aliases or alter the user-visible grammar.
std::wstring normalize(std::wstring value);

}  // namespace qingying::command_detail
