#pragma once

#include "qingying/action/action_dispatcher.hpp"
namespace qingying {

class CaptureService;
class ResultStore;
class ResultActionService;
class ExportExecutor;

void registerAppHandlers(ActionDispatcher& dispatcher,
                         CaptureService& capture_service,
                         ResultStore& results,
                         ResultActionService& result_actions,
                         ExportExecutor* export_executor = nullptr);

void registerAsyncSaveHandler(ActionDispatcher& dispatcher,
                              ResultActionService& result_actions,
                              ExportExecutor& export_executor);

}  // namespace qingying
