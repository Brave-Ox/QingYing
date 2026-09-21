#include "qingying/command/command_parse_diagnostics.hpp"

namespace qingying {

const wchar_t* commandParseStageName(CommandParseStage stage) noexcept {
  switch (stage) {
    case CommandParseStage::Normalize: return L"normalize";
    case CommandParseStage::MatchRule: return L"match-rule";
    case CommandParseStage::ValidatePlan: return L"validate-plan";
    case CommandParseStage::Complete: return L"complete";
    case CommandParseStage::None: break;
  }
  return L"none";
}

}  // namespace qingying
