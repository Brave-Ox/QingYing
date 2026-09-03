#pragma once

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/capture/capture_engine.hpp"

#include <functional>

namespace qingying {

class ResultStore;
class ResultActionService;

// Optional composition hook used by deterministic callers/tests. When empty,
// the CaptureEngine supplied to registerAppHandlers is used directly.
using CaptureRegionInvoker =
    std::function<ActionResult(const ActionRequest&, Image&)>;

void registerAppHandlers(ActionDispatcher& dispatcher, CaptureEngine& capture,
                          ResultStore& results,
                          ResultActionService& result_actions,
                          CaptureRegionInvoker capture_region = {});

}  // namespace qingying
