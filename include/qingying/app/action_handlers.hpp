#pragma once

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/capture_session.hpp"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"

namespace qingying {

void registerAppHandlers(ActionDispatcher& dispatcher, CaptureEngine& capture,
                         ExportService& export_service, CaptureSession& session);

}  // namespace qingying
