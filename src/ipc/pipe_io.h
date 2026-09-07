#pragma once

#include "pipe_identity.h"
#include "automation_wire_codec.h"
#include "qingying/ipc/pipe_server.h"
#include <deque>
#include <mutex>
#include <thread>

namespace qingying::ipc::detail {
using Deadline = std::chrono::steady_clock::time_point;
inline Deadline after(std::chrono::milliseconds duration) {
  return std::chrono::steady_clock::now() + duration;
}
bool validOptions(const PipeOptions& options);
// Every operation owns its event/OVERLAPPED until GetOverlappedResult has
// collected success, error or cancellation. Only the I/O worker waits here.
bool pipeConnect(HANDLE pipe, HANDLE stop);
bool pipeTransfer(HANDLE pipe, HANDLE stop, bool writing, void* buffer,
                  DWORD size, DWORD& transferred, Deadline deadline);

class PipeStream final {
 public:
  explicit PipeStream(PipeOptions options);
  ~PipeStream();
  Handle pipe;
  bool startWriter();
  void cancel() noexcept;
  void joinWriter() noexcept;
  bool stopped() const noexcept;
  bool send(const WireMessage& message) noexcept;
  std::optional<WireMessage> read(Deadline deadline);
  HANDLE stopEvent() const { return stop_.get(); }
 private:
  void writeLoop() noexcept;
  PipeOptions options_;
  Handle stop_;
  Handle wake_;
  std::mutex mutex_;
  std::deque<std::string> output_;
  std::uint64_t output_bytes_{0};
  std::thread writer_;
};
}  // namespace qingying::ipc::detail
