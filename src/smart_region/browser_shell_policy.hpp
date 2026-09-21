#pragma once

#include "qingying/window/smart_region_types.hpp"

#include "browser_shell_types.hpp"

namespace qingying {

bool shouldUseBrowserShellPolicy(
    const SmartRegionWindowSnapshot& snapshot) noexcept;
bool acceptsBrowserShellCandidate(
    const SmartRegionCandidate& candidate, BrowserShellRole role) noexcept;

}  // namespace qingying
