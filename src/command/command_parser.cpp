#include "qingying/command/command_parser.hpp"

namespace qingying {

bool CommandParser::tryParse(const std::wstring& /*utterance*/,
                             ActionRequest* /*out*/) const {
  return false;
}

}  // namespace qingying
