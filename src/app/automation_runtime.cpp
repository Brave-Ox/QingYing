#include "qingying/app/automation_runtime.h"
namespace qingying {
AutomationRuntime::AutomationRuntime(AutomationEndpoint& endpoint, UiActionScheduler& scheduler,
    ipc::PipeOptions options) : endpoint_(endpoint), scheduler_(scheduler), options_(std::move(options)) {}
AutomationRuntime::~AutomationRuntime() { shutdown(); }
bool AutomationRuntime::enable() {
  if (stopped_) return false;
  if (pipe_) return true;
  auto pipe = std::make_unique<ipc::PipeServer>(ipc::PipeServer::Hooks{
    [this] { return endpoint_.connectAuthenticated(); },
    [this](TrustedAutomationContext context, AutomationRequest request, AutomationCompletion completion) {
      scheduler_.submit(std::move(context), std::move(request), std::move(completion));
    },
    [this](const TrustedAutomationContext& context) { scheduler_.disconnect(context); },
    [this](const TrustedAutomationContext& context) { endpoint_.disconnect(context); }
  }, options_);
  if (!pipe->start()) return false;
  pipe_ = std::move(pipe);
  endpoint_.setTransportEnabled(true);
  return true;
}
void AutomationRuntime::disable() {
  endpoint_.setTransportEnabled(false);
  if (pipe_) { pipe_->stop(); pipe_.reset(); }
  scheduler_.drain();
}
void AutomationRuntime::tick() {
  if (stopped_) return;
  if (pipe_) pipe_->drain();
  endpoint_.tick();
}
void AutomationRuntime::stopAccepting() {
  if (stopped_) {
    if (pipe_) pipe_->stopAccepting();
    return;
  }
  stopped_ = true;
  endpoint_.setTransportEnabled(false);
  // Revoke first. Workers do not depend on UI dispatch and will observe stop.
  if (pipe_) pipe_->stopAccepting();
}
void AutomationRuntime::shutdownTransport() {
  (void)shutdownTransportUntil(
      (std::chrono::steady_clock::time_point::max)());
}
bool AutomationRuntime::shutdownTransportUntil(
    std::chrono::steady_clock::time_point deadline) noexcept {
  stopAccepting();
  if (!pipe_) return true;
  if (!pipe_->joinUntil(deadline)) return false;
  pipe_.reset();
  return true;
}
std::string AutomationRuntime::transportDiagnosticSnapshot() const {
  return pipe_ ? pipe_->diagnosticSnapshot()
               : "thread=pipe_worker request_id=0 plugin_id=n/a "
                 "queue_length=0 active_workers=0 last_progress=not_running";
}
void AutomationRuntime::shutdown() {
  if (shutdown_complete_) return;
  stopAccepting();
  endpoint_.shutdown(); // joins business producers, drains and releases scopes
  shutdownTransport(); // collect I/O before destroying HWND
  shutdown_complete_ = true;
}
}  // namespace qingying
