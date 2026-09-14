#include "qingying/app/action_handlers.hpp"

#include "qingying/action/i_async_action_handler.h"
#include "qingying/app/export_executor.h"
#include "qingying/app/result_action_service.h"
#include "qingying/automation/automation_contract.h"

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

namespace qingying {
namespace {

ActionResult failure(int code, const char* message, const char* stage) {
  ActionResult result;
  result.error_code = code;
  result.message = message;
  result.failure_stage = stage;
  return result;
}

std::optional<ActionResult> pendingAbort(const ActionRequest& request,
                                         const ExportExecutor& executor) {
  if (executor.stopping())
    return failure(ErrorCode::kShuttingDown, "export executor is stopping",
                   "export_queue");
  if (request.operation_control) {
    const auto status = request.operation_control->status();
    if (status.abort_reason == AbortReason::Deadline)
      return failure(ErrorCode::kTimeout, "save timed out before encoding",
                     "export_queue");
    if (status.abort_reason != AbortReason::None)
      return failure(ErrorCode::kCancelled, "save cancelled before encoding",
                     "export_queue");
  }
  if (request.timedOut())
    return failure(ErrorCode::kTimeout, "save timed out before encoding",
                   "export_queue");
  if (request.cancellation.isCancellationRequested())
    return failure(ErrorCode::kCancelled, "save cancelled before encoding",
                   "export_queue");
  return std::nullopt;
}

void normalizeCommitRejection(const ActionRequest& request,
                              const ExportExecutor& executor,
                              ActionResult& result) {
  if (result.ok || result.error_code != ErrorCode::kCancelled) return;
  if (request.operation_control) {
    const auto status = request.operation_control->status();
    if (status.committed) return;
    if (status.abort_reason == AbortReason::Deadline) {
      result.error_code = ErrorCode::kTimeout;
      result.message = "save timed out before commit";
      return;
    }
  }
  if (executor.stopping()) {
    result.error_code = ErrorCode::kShuttingDown;
    result.message = "save stopped before commit";
  }
}

struct AsyncSaveState {
  ResultActionService::PreparedSave task;
  ActionRequest request;
  ActionCompletion completion;
  std::atomic_bool completed{false};

  void complete(ActionResult result) {
    if (completed.exchange(true, std::memory_order_acq_rel)) return;
    if (completion) completion(std::move(result));
  }
};

class AsyncSaveHandler final : public IAsyncActionHandler {
 public:
  AsyncSaveHandler(ResultActionService& actions, ExportExecutor& executor)
      : actions_(actions), executor_(executor) {}

  ActionType type() const override { return ActionType::Save; }

  void handleAsync(const ActionRequest& request, ActionCompletion completion,
                   const ActionExecutor& /*executor*/) override {
    const auto* payload = std::get_if<SaveRequest>(&request.payload);
    if (payload == nullptr) {
      completion(failure(ErrorCode::kInvalidArgument, "invalid save payload",
                         "prepare_save"));
      return;
    }

    ResultActionService::CommitAuthorization authorize;
    if (request.operation_control) {
      authorize = [control = request.operation_control,
                   executor = &executor_] {
        return !executor->stopping() && control->tryCommit();
      };
    } else {
      authorize = [request, executor = &executor_] {
        return !executor->stopping() && !request.timedOut() &&
               !request.cancellation.isCancellationRequested();
      };
    }

    ResultActionService::PreparedSave task;
    auto prepared = actions_.prepareSave(request.context.result_scope,
        payload->result, payload->path, std::move(authorize), &task,
        payload->overwrite);
    if (!prepared.ok) {
      completion(std::move(prepared));
      return;
    }

    auto state = std::make_shared<AsyncSaveState>();
    state->task = std::move(task);
    state->request = request;
    state->completion = std::move(completion);
    const bool accepted = executor_.submit(
        [this, state] {
          if (auto aborted = pendingAbort(state->request, executor_)) {
            state->complete(std::move(*aborted));
            return;
          }
          ActionResult result;
          try {
            result = actions_.executeSave(std::move(state->task));
          } catch (...) {
            result = failure(ErrorCode::kExportFailed,
                             "save worker threw an exception", "encode");
          }
          normalizeCommitRejection(state->request, executor_, result);
          state->complete(std::move(result));
        },
        [state] {
          state->complete(failure(ErrorCode::kShuttingDown,
              "queued save rejected during shutdown", "export_queue"));
        },
        request.request_id, "save");
    if (!accepted) {
      state->complete(failure(executor_.stopping() ? ErrorCode::kShuttingDown
                                                   : ErrorCode::kResourceLimit,
          executor_.stopping() ? "export executor is stopping"
                               : "export queue is full",
          "export_queue"));
    }
  }

 private:
  ResultActionService& actions_;
  ExportExecutor& executor_;
};

}  // namespace

void registerAsyncSaveHandler(ActionDispatcher& dispatcher,
                              ResultActionService& result_actions,
                              ExportExecutor& export_executor) {
  dispatcher.registerAsyncHandler(
      std::make_unique<AsyncSaveHandler>(result_actions, export_executor));
}

}  // namespace qingying
