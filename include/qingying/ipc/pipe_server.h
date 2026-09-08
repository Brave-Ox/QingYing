#pragma once

#include "qingying/automation/automation_contract.h"
#include <memory>

namespace qingying::ipc {
struct PipeOptions {
  AutomationLimits limits;
  std::chrono::milliseconds handshake_timeout{3000};
  std::chrono::milliseconds write_timeout{3000};
  // Empty selects the production logon endpoint. Tests use an alphanumeric
  // suffix; arbitrary names (including remote paths) are never accepted.
  std::wstring test_suffix;
  std::uint32_t io_chunk_bytes{4096};
};

// Each server object starts once; re-enable constructs a new server while the
// endpoint continues to allocate unique generations.
// Construct/start/drain/stop on the owner (UI) thread. drain() never waits for
// I/O. Call it periodically, including when UI wake messages were lost.
// Hooks must not throw or reenter this server. connect/submit/disconnect run
// on the owner; revoke runs on an I/O thread (or owner during stop) and MUST
// synchronously revoke scheduler admission, without waiting for UI work.
// Typical bindings: Endpoint::connectAuthenticated, Scheduler::submit,
// Scheduler::disconnect, Endpoint::disconnect. No business objects on I/O.
class PipeServer final {
 public:
  struct Hooks {
    std::function<std::optional<TrustedAutomationContext>()> connect;
    std::function<void(TrustedAutomationContext, AutomationRequest,
                       AutomationCompletion)> submit;
    std::function<void(const TrustedAutomationContext&)> revoke;
    std::function<void(const TrustedAutomationContext&)> disconnect;
  };
  explicit PipeServer(Hooks hooks, PipeOptions options = {});
  ~PipeServer();
  bool start();
  void drain();
  // Owner thread: revoke admission and signal cancellation without joining.
  void stopAccepting() noexcept;
  void stop() noexcept;
  std::wstring name() const;
  std::uint32_t lastError() const noexcept;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace qingying::ipc
