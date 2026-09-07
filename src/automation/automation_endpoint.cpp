#include "qingying/automation/automation_endpoint.h"

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/capture_workflow.hpp"
#include "qingying/app/result_store.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace qingying {
namespace {
AutomationResponse responseWith(int code) {
  AutomationResponse response;
  response.result.ok = code == ErrorCode::kOk;
  response.result.error_code = code;
  response.result.message = std::string(errorCodeSymbol(code));
  return response;
}
const char* capability(ActionType type) {
  switch (type) {
    case ActionType::CaptureRegion: return "capture_region";
    case ActionType::CaptureWindow: return "capture_window";
    case ActionType::CropCenter: return "crop_center";
    case ActionType::Copy: return "copy";
    case ActionType::Save: return "save";
    case ActionType::Pin: return "pin";
    default: return "";
  }
}
}
AutomationEndpoint::AutomationEndpoint(ActionDispatcher& dispatcher,
    CaptureWorkflow& workflow, ResultStore& results, OperationRegistry& registry,
    UiActionScheduler& scheduler, InteractionGate& gate, AutomationLimits limits,
    ExecutionPolicy policy)
    : dispatcher_(dispatcher), workflow_(workflow), results_(results),
      registry_(registry), scheduler_(scheduler), gate_(gate), limits_(limits),
      policy_(std::move(policy)) {
  if (!limits_.valid()) throw std::invalid_argument("endpoint limits");
  scheduler_.setSettlementHooks(
      [this](UiMessageToken ticket, AutomationResponse& response) { settle(ticket, response); },
      [this](UiMessageToken ticket) { executions_.erase(ticket); });
}
AutomationEndpoint::~AutomationEndpoint() {
  shutdown();
  scheduler_.setSettlementHooks({}, {});
}
void AutomationEndpoint::checkThread() const {
  if (std::this_thread::get_id() != ui_thread_)
    throw std::logic_error("automation endpoint requires UI thread");
}
bool AutomationEndpoint::connected(const TrustedAutomationContext& context) const {
  const auto it = connections_.find(context.connection.generation);
  return context.valid() && it != connections_.end() &&
      sameConnection(it->second.connection, context.connection) &&
      it->second.action.result_scope == context.action.result_scope;
}
std::optional<TrustedAutomationContext> AutomationEndpoint::connectAuthenticated() {
  checkThread();
  if (stopping_ || connections_.size() >= limits_.max_connections ||
      next_scope_ == (std::numeric_limits<ResultScopeId>::max)()) return std::nullopt;
  auto context = registry_.connect(next_scope_++);
  if (!context) return std::nullopt;
  if (!scheduler_.connect(*context)) {
    registry_.disconnect(*context);
    return std::nullopt;
  }
  connections_.emplace(context->connection.generation, *context);
  return context;
}
void AutomationEndpoint::disconnect(const TrustedAutomationContext& context) {
  checkThread();
  if (!connected(context)) return;
  // Revoke admission/generation before clearing UI-owned state. A running
  // executor retains its guard until actual cleanup/completion is delivered.
  scheduler_.disconnect(context);
  registry_.disconnect(context);
  connections_.erase(context.connection.generation);
  results_.clearScope(context.action.result_scope);
}
StatusInfo AutomationEndpoint::status() const {
  checkThread();
  StatusInfo info;
  info.reachable = true;
  info.app_running = !stopping_;
  info.automation_enabled = policy_.transport_enabled && !stopping_;
  info.connection_reason = stopping_ ? "stopping" :
      policy_.transport_enabled ? "enabled" : "transport_not_enabled";
  info.busy = gate_.busy();
  info.busy_reason = gate_.reason();
  if (!stopping_) {
    info.capabilities = {"status", "get_operation", "cancel_operation", "release_result"};
    for (auto type : policy_.ready_actions) {
      const std::string name = capability(type);
      if (!name.empty() && std::find(info.capabilities.begin(), info.capabilities.end(), name) == info.capabilities.end())
        info.capabilities.push_back(name);
    }
  }
  info.limits = limits_;
  const auto budget = results_.budgetSnapshot();
  ResourceUsage resources;
  resources.result_bytes = budget.retained_bytes;
  resources.reserved_result_bytes = budget.reserved_bytes;
  info.resources = resources;
  info.queues = scheduler_.queueUsage();
  return info;
}
void AutomationEndpoint::execute(UiMessageToken ticket,
    const TrustedAutomationContext& context, const AutomationRequest& request,
    std::shared_ptr<OperationControl> control) {
  checkThread();
  auto reply = [&](AutomationResponse response) { scheduler_.complete(ticket, std::move(response)); };
  if (stopping_) { reply(responseWith(ErrorCode::kShuttingDown)); return; }
  if (!connected(context)) { reply(responseWith(ErrorCode::kAccessDenied)); return; }
  if (!validateAutomationRequest(request, limits_).valid) {
    reply(responseWith(ErrorCode::kInvalidArgument)); return;
  }
  if (const auto* get = std::get_if<GetOperationRequest>(&request.payload)) {
    tick();
    const auto snapshot = registry_.get(context, get->operation_id);
    auto response = responseWith(snapshot ? ErrorCode::kOk : ErrorCode::kOperationNotFound);
    if (snapshot) { response.control = *snapshot; response.result.operation_id = snapshot->operation_id; }
    reply(std::move(response)); return;
  }
  if (const auto* cancel = std::get_if<CancelOperationRequest>(&request.payload)) {
    std::optional<CancellationResult> result;
    if (const auto* target = std::get_if<OperationCancellation>(&cancel->target)) {
      result = registry_.cancel(context, target->operation_id);
    } else {
      const auto id = std::get<RequestCancellation>(cancel->target).request_id;
      result = scheduler_.cancellationReceipt(ticket);
      // When an operation already exists, return its current state while
      // retaining the admission receipt even if cancellation was already set.
      for (const auto& entry : executions_) {
        const auto& execution = entry.second;
        if (!sameConnection(execution->context.connection, context.connection)) continue;
        const auto snapshot = registry_.get(context, execution->operation_id);
        // Correlation lives in the trusted execution, never the wire payload.
        const bool matches = execution->request_id == id ||
            std::any_of(execution->waiters.begin(), execution->waiters.end(),
                [id](const auto& waiter) { return waiter.second == id; });
        if (!matches || !snapshot) continue;
        if (result) {
          result->operation_id = snapshot->operation_id;
          result->state = snapshot->state;
        }
        break;
      }
    }
    auto response = responseWith(result ? ErrorCode::kOk : ErrorCode::kOperationNotFound);
    if (result) { response.control = *result; response.result.operation_id = result->operation_id; }
    reply(std::move(response)); return;
  }
  if (const auto* release = std::get_if<ReleaseResultRequest>(&request.payload)) {
    const auto id = release->result_id;
    const bool was_available = results_.resultStatus(context.action.result_scope, id) == ErrorCode::kOk;
    const auto code = results_.releaseResult(context.action.result_scope, id);
    auto response = responseWith(code);
    if (code == ErrorCode::kOk) {
      registry_.invalidateResult(context, id, ResultAvailability::Released);
      response.control = ReleasedResult{id, !was_available};
    }
    reply(std::move(response)); return;
  }
  const auto* action = std::get_if<ExecuteActionRequest>(&request.payload);
  const auto type = action ? actionType(action->payload) : ActionType::LongShotRegion;
  if (type == ActionType::Status) {
    auto response = responseWith(ErrorCode::kOk);
    response.result.output = status();
    reply(std::move(response)); return;
  }
  const bool ready = action && std::find(policy_.ready_actions.begin(),
      policy_.ready_actions.end(), type) != policy_.ready_actions.end();
  if (!ready) {
    reply(responseWith(gate_.busy() ? ErrorCode::kBusy : ErrorCode::kNotImplemented)); return;
  }
  const auto submission = registry_.begin(context, request, std::move(control));
  if (!submission.ok()) { reply(responseWith(submission.error_code)); return; }
  if (submission.reused) {
    const auto snapshot = registry_.get(context, submission.operation_id);
    if (snapshot && snapshot->outcome) {
      auto response = responseWith(ErrorCode::kOk);
      response.result = *snapshot->outcome;
      reply(std::move(response)); return;
    }
    for (const auto& entry : executions_) {
      if (entry.second->operation_id == submission.operation_id) {
        auto execution = entry.second;
        scheduler_.shareControl(ticket,
            std::dynamic_pointer_cast<OperationControl>(submission.control));
        execution->waiters.emplace_back(ticket, request.request_id);
        executions_.emplace(ticket, std::move(execution));
        return;
      }
    }
    reply(responseWith(ErrorCode::kNotReady)); return;
  }
  auto guard = gate_.acquire(InteractionKind::Capture);
  auto execution = std::make_shared<Execution>(Execution{context,
      submission.operation_id, request.request_id, std::move(guard), {}, {}});
  executions_.emplace(ticket, execution);
  if (!execution->guard) { reply(responseWith(ErrorCode::kBusy)); return; }
  registry_.advance(context, submission.operation_id, OperationState::Running);
  ActionRequest dispatched{action->payload};
  dispatched.request_id = request.request_id;
  dispatched.operation_id = submission.operation_id;
  dispatched.context = context.action;
  dispatched.cancellation = context.cancellation;
  dispatched.submitted_at = context.submitted_at;
  dispatched.timeout = automationTimeout(request, limits_);
  dispatched.operation_control = submission.control;
  // Completion may come from a worker; only scheduler.complete is thread-safe.
  // The handler acknowledges worker/overlay/pin cleanup before calling back.
  dispatcher_.submit(dispatched, [scheduler = &scheduler_, ticket](ActionResult result) {
    AutomationResponse response;
    response.result = std::move(result);
    scheduler->complete(ticket, std::move(response));
  });
}
void AutomationEndpoint::settle(UiMessageToken ticket, AutomationResponse& response) {
  checkThread();
  const auto it = executions_.find(ticket);
  if (it == executions_.end()) return;
  auto execution = it->second;
  if (!execution->outcome) {
    ActionResult outcome;
    registry_.complete(execution->context, execution->operation_id, response.result, &outcome);
    execution->outcome = std::move(outcome);
    if (!connected(execution->context)) results_.clearScope(execution->context.action.result_scope);
    for (const auto& waiter : execution->waiters) {
      AutomationResponse replay;
      replay.result = *execution->outcome;
      scheduler_.complete(waiter.first, std::move(replay));
    }
  }
  const auto request_id = response.result.request_id;
  response.result = *execution->outcome;
  response.result.request_id = request_id;
}
void AutomationEndpoint::tick() {
  checkThread();
  results_.sweep();
  for (const auto& entry : connections_) {
    const auto& context = entry.second;
    const auto id = registry_.currentResult(context);
    if (id == kInvalidResultId) continue;
    const auto code = results_.resultStatus(context.action.result_scope, id);
    if (code != ErrorCode::kOk) registry_.invalidateResult(context, id,
        code == ErrorCode::kResultExpired ? ResultAvailability::Expired : ResultAvailability::Released);
  }
  registry_.sweep();
  scheduler_.drain();
}
void AutomationEndpoint::shutdown() {
  checkThread();
  if (stopping_) return;
  stopping_ = true;
  gate_.stop();
  // Future transport: stop admission/cancel I/O first; never block the UI on
  // pipe flushing. Business/export workers must join before final reclamation.
  scheduler_.stopAccepting();
  workflow_.shutdown();
  if (policy_.stop_producers) policy_.stop_producers();
  scheduler_.shutdown();
  for (const auto& entry : connections_) registry_.disconnect(entry.second);
  connections_.clear();
  results_.clearAll();
}
}  // namespace qingying
