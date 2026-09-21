#pragma once

#include "qingying/command/command_parser.hpp"

#include <string>

namespace qingying {

// Human-readable plan text for diagnostics, tests and demonstrations.
std::wstring explainCommandPlan(const CommandPlan& plan);

}  // namespace qingying
