#pragma once

#include "qingying/action/action_dispatcher.hpp"
namespace qingying {

class CaptureService;
class ResultStore;
class ResultActionService;

void registerAppHandlers(ActionDispatcher& dispatcher,
                         CaptureService& capture_service,
                         ResultStore& results,
                         ResultActionService& result_actions);

}  // namespace qingying
