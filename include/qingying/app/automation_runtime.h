#pragma once
#include "qingying/automation/automation_endpoint.h"
#include "qingying/ipc/pipe_server.h"
#include <chrono>
#include <string>
namespace qingying {
// Owner-thread composition. The window/message route must outlive shutdown.
class AutomationRuntime final {
 public:
  AutomationRuntime(AutomationEndpoint& endpoint, UiActionScheduler& scheduler,
                    ipc::PipeOptions options = {});
  ~AutomationRuntime();
  bool enable();
  void disable();
  void tick();
  // UI wake path; performs no lease/deadline housekeeping.
  void drainTransport();
  // Phase 1 of application shutdown: revoke transport admission without
  // waiting for I/O workers.
  void stopAccepting();
  // Final transport phase: join PipeServer workers and release pipe handles.
  bool shutdownTransportUntil(
      std::chrono::steady_clock::time_point deadline) noexcept;
  std::string transportDiagnosticSnapshot() const;
  void shutdownTransport();
  void shutdown();
  bool enabled() const { return pipe_ != nullptr; }
 private:
  AutomationEndpoint& endpoint_;
  UiActionScheduler& scheduler_;
  ipc::PipeOptions options_;
  std::unique_ptr<ipc::PipeServer> pipe_;
  bool stopped_{false};
  bool shutdown_complete_{false};
};
}  // namespace qingying
