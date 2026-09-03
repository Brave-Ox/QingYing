#pragma once

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/capture_session.hpp"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <functional>

namespace qingying {

// Optional composition hook used by deterministic callers/tests. When empty,
// the CaptureEngine supplied to registerAppHandlers is used directly.
using CaptureRegionInvoker =
    std::function<ActionResult(const ActionRequest&, Image&)>;

void registerAppHandlers(ActionDispatcher& dispatcher, CaptureEngine& capture,
                          ExportService& export_service, CaptureSession& session,
                          PinManager& pin_manager,
                          CaptureRegionInvoker capture_region = {});

}  // namespace qingying
