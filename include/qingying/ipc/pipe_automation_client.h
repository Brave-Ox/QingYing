#pragma once

#include "qingying/ipc/pipe_server.h"

namespace qingying::ipc {
// connect() is bounded by handshake_timeout and belongs on the bridge thread.
// Finish connect() before concurrent submit/close calls.
// Each object connects once, never retries/replays. Accepted completions run on
// the reader thread; immediate rejection/close reclamation on the caller.
// Callbacks must not throw. close() is also supported from a completion.
class PipeAutomationClient final : public IAutomationClient {
 public:
  explicit PipeAutomationClient(PipeOptions options = {});
  ~PipeAutomationClient() override;
  bool connect();
  AutomationConnection connection() const;
  std::uint32_t lastError() const noexcept;
  void submit(AutomationRequest request, AutomationCompletion completion) override;
  void close() noexcept override;
 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace qingying::ipc
