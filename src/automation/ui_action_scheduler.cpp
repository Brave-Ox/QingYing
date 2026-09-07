#include "qingying/automation/ui_action_scheduler.h"

#include <stdexcept>

namespace qingying {
namespace {
bool controlLane(const AutomationRequest& request) {
  const auto* execute = std::get_if<ExecuteActionRequest>(&request.payload);
  return execute ? std::holds_alternative<StatusRequest>(execute->payload)
                 : !std::holds_alternative<BeginLongShotRequest>(request.payload);
}
}
UiActionScheduler::UiActionScheduler(Post post, Execute execute,
    AutomationLimits limits, OperationClock clock)
    : post_(std::move(post)), execute_(std::move(execute)), limits_(limits),
      clock_(clock ? std::move(clock) : OperationClock{[] { return std::chrono::steady_clock::now(); }}),
      ui_thread_(std::this_thread::get_id()) {
  if (!post_ || !execute_ || !limits_.valid()) throw std::invalid_argument("scheduler configuration");
}
UiActionScheduler::~UiActionScheduler() { shutdown(); }
void UiActionScheduler::checkThread() const {
  if (std::this_thread::get_id() != ui_thread_) throw std::logic_error("scheduler requires UI thread");
}
bool UiActionScheduler::connected(const TrustedAutomationContext& context) const {
  const auto it = connections_.find(context.connection.generation);
  return context.valid() && context.connection.application_epoch == epoch_ &&
      it != connections_.end() && it->second == context.action.result_scope;
}
bool UiActionScheduler::connect(const TrustedAutomationContext& context) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!accepting_ || !context.valid() || connections_.size() >= limits_.max_connections ||
      (epoch_ && epoch_ != context.connection.application_epoch) ||
      context.connection.generation <= last_generation_) return false;
  connections_.emplace(context.connection.generation, context.action.result_scope);
  epoch_ = context.connection.application_epoch;
  last_generation_ = context.connection.generation;
  return true;
}
AutomationResponse UiActionScheduler::failure(const Entry& entry, int code) {
  AutomationResponse response;
  response.connection = entry.context.connection;
  response.result.request_id = entry.request.request_id;
  response.result.error_code = code;
  return response;
}
void UiActionScheduler::submit(TrustedAutomationContext context, AutomationRequest request,
                               AutomationCompletion completion) {
  Entry entry{std::move(context), std::move(request), std::move(completion)};
  int error = ErrorCode::kCancelled;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (accepting_ && connected(entry.context)) {
      error = ErrorCode::kInvalidArgument;
      if (validateAutomationRequest(entry.request, limits_).valid) {
        entry.control_lane = controlLane(entry.request);
        std::size_t global = 0, local = 0;
        bool duplicate = false;
        for (const auto& pair : entries_) {
          const auto& other = pair.second;
          const bool same = sameConnection(other.context.connection, entry.context.connection);
          duplicate |= same && other.request.request_id == entry.request.request_id;
          if (other.control_lane == entry.control_lane) { ++global; if (same) ++local; }
        }
        const auto max_global = entry.control_lane ? limits_.max_control_requests_global : limits_.max_queued_requests_global;
        const auto max_local = entry.control_lane ? limits_.max_control_requests_per_connection : limits_.max_queued_requests_per_connection;
        error = duplicate ? ErrorCode::kInvalidArgument : ErrorCode::kBusy;
        if (!duplicate && global < max_global && local < max_local) {
          entry.control = std::make_shared<OperationControl>(entry.context, entry.request.request_id,
              automationTimeout(entry.request, limits_), clock_);
          auto token = channel_.push(AutomationRequestMessage{});
          if (token) {
            auto it = entries_.emplace(*token, std::move(entry)).first;
            if (post_(WM_QINGYING_AUTOMATION_REQUEST, *token)) {
              // Private transport cancellation must reach a queued request
              // before the UI allocates its OperationId.
              const auto* cancel_request = std::get_if<CancelOperationRequest>(&it->second.request.payload);
              const auto* target = cancel_request
                  ? std::get_if<RequestCancellation>(&cancel_request->target) : nullptr;
              if (target) {
                for (auto& pair : entries_) {
                  auto& other = pair.second;
                  if (sameConnection(other.context.connection, it->second.context.connection) &&
                      other.request.request_id == target->request_id)
                    other.control->requestCancel(AbortReason::ClientCancel);
                }
              }
              return;
            }
            channel_.discard(*token);
            entry = std::move(it->second);
            entries_.erase(it);
          }
          error = ErrorCode::kUnknown;
        }
      }
    }
  }
  if (entry.completion) entry.completion(failure(entry, error));
}
bool UiActionScheduler::cancel(const TrustedAutomationContext& context, RequestId request,
                               AbortReason reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected(context)) return false;
  for (auto& pair : entries_) {
    auto& entry = pair.second;
    if (sameConnection(context.connection, entry.context.connection) && entry.request.request_id == request)
      return entry.control->requestCancel(reason);
  }
  return false;
}
bool UiActionScheduler::complete(UiMessageToken ticket, AutomationResponse response) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = entries_.find(ticket);
  if (it == entries_.end() || !it->second.running || it->second.response) return false;
  auto& entry = it->second;
  response.connection = entry.context.connection;
  response.result.request_id = entry.request.request_id;
  entry.response = std::move(response);
  const auto token = channel_.push(AutomationCompletionMessage{ticket});
  if (token) {
    if (post_(WM_QINGYING_AUTOMATION_COMPLETE, *token)) entry.completion_token = *token;
    else channel_.discard(*token);
  }
  return true;
}
void UiActionScheduler::settle(UiMessageToken ticket) {
  Entry entry;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(ticket);
    if (it == entries_.end() || !it->second.response) return;
    entry = std::move(it->second);
    entries_.erase(it);
    channel_.discard(ticket);
    channel_.discard(entry.completion_token);
  }
  if (entry.completion) entry.completion(std::move(*entry.response));
}
void UiActionScheduler::dispatch(UINT message, UiMessageToken token) {
  checkThread();
  if (message == WM_QINGYING_AUTOMATION_COMPLETE) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (entries_.find(token) != entries_.end()) return;
    }
    auto event = channel_.take<AutomationCompletionMessage>(token);
    if (event) settle(event->ticket);
    return;
  }
  if (message != WM_QINGYING_AUTOMATION_REQUEST) return;
  TrustedAutomationContext context;
  AutomationRequest request;
  std::shared_ptr<OperationControl> control;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = entries_.find(token);
    if (it == entries_.end() || it->second.running ||
        !channel_.take<AutomationRequestMessage>(token)) return;
    auto& entry = it->second;
    if (!accepting_ || !connected(entry.context)) entry.control->requestCancel(AbortReason::Disconnect);
    const auto status = entry.control->status();
    if (status.abort_reason != AbortReason::None) {
      entry.response = failure(entry, status.abort_reason == AbortReason::Deadline ? ErrorCode::kTimeout : ErrorCode::kCancelled);
    } else {
      entry.running = true;
      context = entry.context;
      request = entry.request;
      control = entry.control;
    }
  }
  if (!control) { settle(token); return; }
  try { execute_(token, context, request, std::move(control)); }
  catch (...) {
    AutomationResponse response;
    response.result.error_code = ErrorCode::kUnknown;
    complete(token, std::move(response));
  }
}
void UiActionScheduler::abortQueued(AbortReason reason, const TrustedAutomationContext* context) {
  for (auto& pair : entries_) {
    auto& entry = pair.second;
    if (context && !sameConnection(context->connection, entry.context.connection)) continue;
    entry.control->requestCancel(reason);
    if (!entry.running && !entry.response) entry.response = failure(entry, ErrorCode::kCancelled);
  }
}
void UiActionScheduler::disconnect(const TrustedAutomationContext& context) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected(context)) return;
  connections_.erase(context.connection.generation);
  abortQueued(AbortReason::Disconnect, &context);
}
void UiActionScheduler::stopAccepting() {
  std::lock_guard<std::mutex> lock(mutex_);
  accepting_ = false;
  abortQueued(AbortReason::Shutdown, nullptr);
}
void UiActionScheduler::drain() {
  checkThread();
  std::vector<UiMessageToken> ready;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& pair : entries_) if (pair.second.response) ready.push_back(pair.first);
  }
  for (auto ticket : ready) settle(ticket);
}
void UiActionScheduler::shutdown() {
  checkThread();
  stopAccepting();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& pair : entries_) if (!pair.second.response)
      pair.second.response = failure(pair.second, ErrorCode::kCancelled);
    connections_.clear();
  }
  drain();
}
std::size_t UiActionScheduler::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return entries_.size();
}
}  // namespace qingying

