#pragma once

#include "qingying/ipc/pipe_server.h"

namespace qingying::ipc {
enum class ClientCloseReason { None, ClientClose, PeerDisconnect, ProtocolError, ReaderException, ConnectFailure };
enum class PipeClientState { Idle, Connecting, Connected, Closing, Closed, Failed };
struct PipeClientDiagnostics {
  AutomationConnection connection;
  ClientCloseReason close_reason{ClientCloseReason::None};
  std::uint32_t last_error{0};
  RequestId last_frame_request{0};
  std::size_t pending_callbacks{0};
  bool accepting{false};
  bool reader_running{false};
  bool reader_exception{false};
  PipeClientState state{PipeClientState::Idle};
};
// connect() is bounded by handshake_timeout and belongs on the bridge thread.
// Finish connect() before concurrent submit/close calls.
// Each object connects once, never retries/replays. Accepted completions run on
// the owned reader thread; immediate rejection runs on the submitting thread.
// Off-reader close joins and guarantees all accepted callbacks have finished.
// Self-close cancels without waiting; remaining callbacks settle after the
// current callback returns. Self-destruction hands ownership to a join reaper.
// Callback targets must outlive off-reader close or be captured weakly.
// The independent ConnectionLifetime owns buffers and callbacks until reader
// exit; callbacks never access the client object through an internal pointer.
class PipeAutomationClient final : public IAutomationClient {
 public:
  explicit PipeAutomationClient(PipeOptions options = {});
  ~PipeAutomationClient() override;
  PipeAutomationClient(const PipeAutomationClient&) = delete;
  PipeAutomationClient& operator=(const PipeAutomationClient&) = delete;
  bool connect();
  AutomationConnection connection() const;
  std::uint32_t lastError() const noexcept;
  PipeClientDiagnostics diagnostics() const;
  void submit(AutomationRequest request, AutomationCompletion completion) override;
  void close() noexcept override;
 private:
  struct ConnectionLifetime;
  std::shared_ptr<ConnectionLifetime> impl_;
};
}  // namespace qingying::ipc
