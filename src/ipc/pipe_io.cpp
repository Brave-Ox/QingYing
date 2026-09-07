#include "pipe_io.h"

#include <algorithm>

namespace qingying::ipc::detail {
namespace {
DWORD remaining(Deadline deadline) {
  if (deadline == Deadline::max()) return INFINITE;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now()).count();
  return ms <= 0 ? 0 : static_cast<DWORD>((std::min)(ms + 1, 0x7fffffffLL));
}
class Pending final {
 public:
  explicit Pending(HANDLE pipe) : pipe_(pipe), event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
    overlapped.hEvent = event_.get();
  }
  ~Pending() {
    const DWORD error = GetLastError();
    if (pending_) {
      CancelIoEx(pipe_, &overlapped);
      DWORD ignored = 0;
      GetOverlappedResult(pipe_, &overlapped, &ignored, TRUE);
    }
    SetLastError(error);
  }
  bool wait(HANDLE stop, Deadline deadline, DWORD& bytes) {
    pending_ = true;
    HANDLE events[] = {stop, event_.get()};
    const auto result = WaitForMultipleObjects(2, events, FALSE, remaining(deadline));
    if (result != WAIT_OBJECT_0 + 1) {
      SetLastError(result == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED);
      return false;
    }
    const bool success = GetOverlappedResult(pipe_, &overlapped, &bytes, FALSE) != FALSE;
    pending_ = false;
    return success;
  }
  bool valid() const { return static_cast<bool>(event_); }
  OVERLAPPED overlapped{};
 private:
  HANDLE pipe_;
  Handle event_;
  bool pending_{false};
};
}
bool validOptions(const PipeOptions& options) {
  return options.limits.valid() && options.limits.max_connections <= 4 &&
      options.limits.max_frame_bytes <= 65536 && options.io_chunk_bytes > 0 &&
      options.io_chunk_bytes <= 65536 && options.handshake_timeout.count() > 0 &&
      options.handshake_timeout <= std::chrono::minutes{1} &&
      options.write_timeout.count() > 0 && options.write_timeout <= std::chrono::minutes{1};
}
bool pipeConnect(HANDLE pipe, HANDLE stop) {
  Pending io(pipe);
  if (!io.valid()) return false;
  if (ConnectNamedPipe(pipe, &io.overlapped)) return true;
  const DWORD error = GetLastError();
  if (error == ERROR_PIPE_CONNECTED) return true;
  DWORD ignored = 0;
  return error == ERROR_IO_PENDING && io.wait(stop, Deadline::max(), ignored);
}
bool pipeTransfer(HANDLE pipe, HANDLE stop, bool writing, void* buffer,
                  DWORD size, DWORD& transferred, Deadline deadline) {
  if (WaitForSingleObject(stop, 0) != WAIT_TIMEOUT) return false;
  if (deadline != Deadline::max() && std::chrono::steady_clock::now() >= deadline) {
    SetLastError(ERROR_TIMEOUT);
    return false;
  }
  Pending io(pipe);
  if (!io.valid()) return false;
  const BOOL ready = writing ? WriteFile(pipe, buffer, size, &transferred, &io.overlapped)
                             : ReadFile(pipe, buffer, size, &transferred, &io.overlapped);
  if (ready) return transferred != 0;
  if (GetLastError() != ERROR_IO_PENDING) return false;
  return io.wait(stop, deadline, transferred) && transferred != 0;
}
PipeStream::PipeStream(PipeOptions options) : options_(std::move(options)),
    stop_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
    wake_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {
  if (!stop_ || !wake_) throw std::runtime_error("pipe event allocation");
}
PipeStream::~PipeStream() { cancel(); joinWriter(); }
bool PipeStream::startWriter() {
  try { writer_ = std::thread([this] { writeLoop(); }); return true; }
  catch (...) { cancel(); return false; }
}
void PipeStream::cancel() noexcept { SetEvent(stop_.get()); }
void PipeStream::joinWriter() noexcept { if (writer_.joinable()) writer_.join(); }
bool PipeStream::stopped() const noexcept {
  return WaitForSingleObject(stop_.get(), 0) != WAIT_TIMEOUT;
}
bool PipeStream::send(const WireMessage& message) noexcept {
  try {
  auto encoded = encodeFrame(message, options_.limits);
  if (!encoded) { cancel(); return false; }
  std::lock_guard<std::mutex> lock(mutex_);
  if (stopped()) return false;
  if (output_.size() >= options_.limits.max_output_frames_per_connection ||
      encoded.bytes.size() > options_.limits.max_output_bytes_per_connection - output_bytes_) {
    cancel(); return false;
  }
  output_bytes_ += encoded.bytes.size();
  output_.push_back(std::move(encoded.bytes));
  SetEvent(wake_.get());
  return true;
  } catch (...) { cancel(); return false; }
}
std::optional<WireMessage> PipeStream::read(Deadline deadline) {
  std::string frame(4, '\0');
  std::size_t offset = 0;
  std::size_t target = 4;
  for (;;) {
    DWORD bytes = 0;
    const DWORD size = static_cast<DWORD>((std::min)(target - offset,
        static_cast<std::size_t>(options_.io_chunk_bytes)));
    if (!pipeTransfer(pipe.get(), stop_.get(), false, &frame[offset], size, bytes, deadline))
      return std::nullopt;
    // Idle authenticated connections can wait indefinitely; a partially sent
    // frame cannot monopolize a slot indefinitely.
    if (deadline == Deadline::max()) deadline = after(options_.handshake_timeout);
    offset += bytes;
    if (offset != target) continue;
    if (target == 4) {
      std::uint32_t length = 0;
      for (unsigned i = 0; i != 4; ++i)
        length |= static_cast<std::uint32_t>(static_cast<unsigned char>(frame[i])) << (i * 8);
      if (!length || length > options_.limits.max_frame_bytes) return std::nullopt;
      target += length;
      frame.resize(target);
    } else {
      auto decoded = decodeBody(std::string_view(frame).substr(4), options_.limits);
      return std::move(decoded.message);
    }
  }
}
void PipeStream::writeLoop() noexcept {
  try {
    HANDLE events[] = {stop_.get(), wake_.get()};
    while (!stopped()) {
      std::string* frame = nullptr;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!output_.empty()) frame = &output_.front();
      }
      if (!frame) {
        if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) break;
        continue;
      }
      // deque retains the front element during push_back; only this writer
      // pops. The active frame continues to count against output limits.
      const auto deadline = after(options_.write_timeout);
      std::size_t offset = 0;
      while (offset < frame->size()) {
        DWORD bytes = 0;
        const DWORD size = static_cast<DWORD>((std::min)(frame->size() - offset,
            static_cast<std::size_t>(options_.io_chunk_bytes)));
        if (!pipeTransfer(pipe.get(), stop_.get(), true, &(*frame)[offset], size, bytes, deadline)) {
          cancel(); return;
        }
        offset += bytes;
      }
      std::lock_guard<std::mutex> lock(mutex_);
      output_bytes_ -= output_.front().size();
      output_.pop_front();
    }
  } catch (...) { cancel(); }
}
}  // namespace qingying::ipc::detail
