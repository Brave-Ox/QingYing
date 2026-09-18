#include "qingying/automation/operation_coordinator.h"
#include "qingying/automation/automation_contract.h"
#include "qingying/diagnostics/fault_boundary.h"
#include "qingying/automation/action_catalog.h"


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
}
OperationCoordinator::OperationCoordinator(AutomationOperationPort& operations,
    AutomationResultPort& results, OperationRegistry& registry,
    UiActionScheduler& scheduler, InteractionGate& gate, AutomationLimits limits,
    ExecutionPolicy policy)
    : operations_(operations), results_(results),
      registry_(registry), scheduler_(scheduler), gate_(gate), limits_(limits),
      policy_(std::move(policy)) {
  if (!limits_.valid()) throw std::invalid_argument("endpoint limits");
  scheduler_.setSettlementHooks(
      [this](UiMessageToken ticket, AutomationResponse& response) { settle(ticket, response); },
      [this](UiMessageToken ticket) { executions_.erase(ticket); });
}
OperationCoordinator::~OperationCoordinator() {
  shutdown();
  scheduler_.setSettlementHooks({}, {});
}
void OperationCoordinator::checkThread() const {
  if (std::this_thread::get_id() != ui_thread_)
    throw std::logic_error("automation endpoint requires UI thread");
}
bool OperationCoordinator::connected(const TrustedAutomationContext& context) const {
  return registry_.isConnected(context);
}
std::optional<TrustedAutomationContext> OperationCoordinator::connectAuthenticated() {
  checkThread();
  if (stopping_ || registry_.activeContexts().size() >= limits_.max_connections ||
      next_scope_ == (std::numeric_limits<ResultScopeId>::max)()) return std::nullopt;
  auto context = registry_.connect(next_scope_++);
  if (!context) return std::nullopt;
  if (!scheduler_.connect(*context)) {
    registry_.disconnect(*context, AutomationSessionCloseReason::AdmissionRejected);
    return std::nullopt;
  }
  return context;
}
void OperationCoordinator::disconnect(const TrustedAutomationContext& context) {
  checkThread();
  if (!connected(context)) return;
  // Revoke admission/generation before clearing UI-owned state. A running
  // executor retains its guard until actual cleanup/completion is delivered.
  scheduler_.disconnect(context);
  registry_.disconnect(context);
  results_.clearScope(context.action.result_scope);
}
StatusInfo OperationCoordinator::status() const {
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
    for (const auto& entry : actionCatalog())
      if (!entry.action || *entry.action == ActionType::Status)
        info.capabilities.push_back(entry.id);
    for (auto type : policy_.ready_actions) {
      const auto descriptor = operations_.describe(type);
      const std::string name = descriptor ? descriptor->capability : "";
      if (!name.empty() && std::find(info.capabilities.begin(), info.capabilities.end(), name) == info.capabilities.end())
        info.capabilities.push_back(name);
    }
  }
  info.limits = limits_;
  const auto budget = results_.budgetSnapshot();
  ResourceUsage resources;
  resources.result_bytes = budget.retained_bytes;
  resources.reserved_result_bytes = budget.reserved_bytes;
  if (policy_.agent_pin_usage) {
    const auto usage = policy_.agent_pin_usage();
    resources.agent_pin_count = usage.first;
    resources.agent_pin_bytes = usage.second;
  }
  info.resources = resources;
  info.queues = scheduler_.queueUsage();
  return info;
}
void OperationCoordinator::setTransportEnabled(bool enabled) {
  checkThread();
  policy_.transport_enabled = enabled && !stopping_;
}
void OperationCoordinator::execute(UiMessageToken ticket,
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
    const auto snapshot = get->operation_handle ? registry_.get(context, *get->operation_handle)
                                               : registry_.get(context, get->operation_id);
    auto response = responseWith(snapshot ? ErrorCode::kOk : ErrorCode::kOperationNotFound);
    if (snapshot) { response.control = *snapshot; response.result.operation_id = snapshot->operation_id; }
    reply(std::move(response)); return;
  }
  if (const auto* cancel = std::get_if<CancelOperationRequest>(&request.payload)) {
    std::optional<CancellationResult> result;
    if (cancel->operation_handle) {
      const auto snapshot = registry_.get(context, *cancel->operation_handle);
      if (snapshot) result = registry_.cancel(context, snapshot->operation_id);
    } else if (const auto* target = std::get_if<OperationCancellation>(&cancel->target)) {
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
    const auto resolved = release->result_handle
        ? registry_.resolveResult(context, *release->result_handle, true)
        : std::optional<ResultId>{release->result_id};
    if (!resolved) { reply(responseWith(ErrorCode::kResultNotFound)); return; }
    const auto id = *resolved;
    const bool was_available = results_.resultStatus(context.action.result_scope, id) == ErrorCode::kOk;
    const auto code = results_.releaseResult(context.action.result_scope, id);
    auto response = responseWith(code);
    if (code == ErrorCode::kOk) {
      registry_.invalidateResult(context, id, ResultAvailability::Released);
      response.control = ReleasedResult{id, !was_available};
    }
    reply(std::move(response)); return;
  }
  AutomationRequest resolved_request = request;
  auto* action = std::get_if<ExecuteActionRequest>(&resolved_request.payload);
  if (action && action->result_handle) {
    const auto resolved = registry_.resolveResult(context, *action->result_handle);
    if (!resolved) { reply(responseWith(ErrorCode::kResultNotFound)); return; }
    if (auto* save = std::get_if<SaveRequest>(&action->payload))
      save->result = ResultSelection::specific(*resolved);
    else if (auto* copy = std::get_if<CopyRequest>(&action->payload))
      copy->result = ResultSelection::specific(*resolved);
    else if (auto* pin = std::get_if<PinRequest>(&action->payload))
      pin->result = ResultSelection::specific(*resolved);
    else { reply(responseWith(ErrorCode::kInvalidArgument)); return; }
    action->result_handle.reset();
    if (!validateAutomationRequest(resolved_request, limits_).valid) {
      reply(responseWith(ErrorCode::kInvalidArgument)); return;
    }
  }
  const auto type = action ? actionType(action->payload) : ActionType::LongShotRegion;
  if (type == ActionType::Status) {
    auto response = responseWith(ErrorCode::kOk);
    response.result.output = status();
    reply(std::move(response)); return;
  }
  const auto descriptor = operations_.describe(type);
  const bool ready = action && descriptor && std::find(policy_.ready_actions.begin(),
      policy_.ready_actions.end(), type) != policy_.ready_actions.end();
  if (!ready) {
    reply(responseWith(gate_.busy() ? ErrorCode::kBusy : ErrorCode::kNotImplemented)); return;
  }
  const auto submission = registry_.begin(context, resolved_request, std::move(control));
  if (!submission.ok()) { reply(responseWith(submission.error_code)); return; }
  if (submission.reused) {
    const auto snapshot = registry_.get(context, submission.operation_id);
    if (snapshot && snapshot->outcome) {
      auto response = responseWith(ErrorCode::kOk);
      response.result = *snapshot->outcome;
      response.operation_handle = submission.handle;
      if (const auto* captured = std::get_if<CapturedResult>(&response.result.output))
        response.result_handle = registry_.bindResult(context, captured->result_id);
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
  const bool handler_owns_interaction = descriptor->owns_interaction;
  auto guard = handler_owns_interaction
      ? InteractionGate::Guard{}
      : gate_.acquire(InteractionKind::Capture);
  auto execution = std::make_shared<Execution>(Execution{context,
      submission.operation_id, submission.handle, request.request_id,
      std::move(guard), {}, {}});
  executions_.emplace(ticket, execution);
  if (!handler_owns_interaction && !execution->guard) {
    reply(responseWith(ErrorCode::kBusy));
    return;
  }
  registry_.advance(context, submission.operation_id, OperationState::Running);
  ActionRequest dispatched{action->payload};
  dispatched.request_id = request.request_id;
  dispatched.operation_id = submission.operation_id;
  dispatched.context = context.action;
  dispatched.cancellation = context.cancellation;
  dispatched.submitted_at = context.submitted_at;
  dispatched.timeout = automationTimeout(request, limits_);
  dispatched.operation_control = submission.control;
  dispatched.diagnostic_context = {context.connection.application_epoch, context.connection.generation,
      context.action.result_scope, request.request_id, submission.operation_id, context.submitted_at};
  // Completion may come from a worker; only scheduler.complete is thread-safe.
  // The handler acknowledges worker/overlay/pin cleanup before calling back.
  operations_.submit(dispatched, [scheduler = &scheduler_, ticket](ActionResult result) {
    AutomationResponse response;
    response.result = std::move(result);
    scheduler->complete(ticket, std::move(response));
  });
}
void OperationCoordinator::settle(UiMessageToken ticket, AutomationResponse& response) {
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
  response.operation_handle = execution->operation_handle;
  if (const auto* captured = std::get_if<CapturedResult>(&response.result.output))
    response.result_handle = registry_.bindResult(execution->context,
                                                   captured->result_id);
}
void OperationCoordinator::tick() {
  checkThread();
  results_.sweep();
  for (const auto& context : registry_.activeContexts()) {
    const auto id = registry_.currentResult(context);
    if (id == kInvalidResultId) continue;
    const auto code = results_.resultStatus(context.action.result_scope, id);
    if (code != ErrorCode::kOk) registry_.invalidateResult(context, id,
        code == ErrorCode::kResultExpired ? ResultAvailability::Expired : ResultAvailability::Released);
  }
  registry_.sweep();
  scheduler_.drain();
}
void OperationCoordinator::beginShutdown() {
  checkThread();
  if (stopping_) return;
  stopping_ = true;
  gate_.stop();
  scheduler_.stopAccepting();
}
void OperationCoordinator::stopBusinessProducers() {
  checkThread();
  beginShutdown();
  if (producers_stopped_) return;
  producers_stopped_ = true;
  if (policy_.stop_producers) policy_.stop_producers();
}
void OperationCoordinator::finishShutdown() {
  checkThread();
  beginShutdown();
  if (shutdown_finished_) return;
  // Business/export workers must join before final callback reclamation.
  scheduler_.shutdown();
  for (const auto& context : registry_.activeContexts())
    registry_.disconnect(context, AutomationSessionCloseReason::Shutdown);
  results_.clearAll();
  shutdown_finished_ = true;
}
void OperationCoordinator::shutdown() {
  checkThread();
  if (shutdown_finished_) return;
  beginShutdown();
  operations_.shutdown();
  stopBusinessProducers();
  finishShutdown();
}
}  // namespace qingying

