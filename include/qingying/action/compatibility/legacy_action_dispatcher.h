#pragma once

#include "qingying/action/compatibility/legacy_action_request.h"

#include <optional>

namespace qingying {

// Compatibility-only adapter. It deliberately lives outside qingying_action so
// the current dispatcher cannot grow a second request path.
// Keep reason: provide one auditable field-to-payload conversion point while
// supported callers migrate.
// Replacement: makeActionRequest() with the corresponding typed payload.
// Removal condition: no supported caller depends on the old field mapping.
// Owner: action/automation maintainers.
[[deprecated(
    "Use qingying::makeActionRequest() with a typed payload instead")]]
std::optional<ActionRequest> adaptLegacyActionRequest(
    const LegacyActionRequest& legacy);

}  // namespace qingying
