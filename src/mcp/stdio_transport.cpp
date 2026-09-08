#include "qingying/mcp/stdio_transport.h"
#include "pipe_identity.h"
#include <atomic>
#include <thread>
#include <stdexcept>

namespace qingying::mcp {
namespace {
bool duplicate(void* supplied, DWORD standard, ipc::detail::Handle& result) {
  HANDLE raw = nullptr;
  HANDLE source = supplied ? supplied : GetStdHandle(standard);
  if (!source || source == INVALID_HANDLE_VALUE ||
      !DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &raw,
                       0, FALSE, DUPLICATE_SAME_ACCESS)) return false;
  result.reset(raw); return true;
}
void diagnostic(HANDLE handle, std::chrono::milliseconds timeout) {
  // Diagnostics never share stdout and a blocked stderr cannot prevent exit.
  std::atomic<bool> done{false};
  std::thread worker([&] {
    const char text[] = "QingYing MCP: stdio transport failed\n";
    DWORD count = 0;
    WriteFile(handle, text, sizeof(text) - 1, &count, nullptr);
    done = true;
  });
  const auto end = std::chrono::steady_clock::now() + timeout;
  while (!done) {
    if (std::chrono::steady_clock::now() >= end) CancelSynchronousIo(worker.native_handle());
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  worker.join();
}
}
StdioTransport::StdioTransport(StdioOptions options) : options_(options) {
  if (!options.max_line_bytes || options.max_line_bytes > 65536 ||
      options.write_timeout.count() <= 0 || options.write_timeout > std::chrono::minutes{1})
    throw std::invalid_argument("stdio options");
}
bool StdioTransport::run(McpBridge& bridge) {
  ipc::detail::Handle input, output, error;
  duplicate(options_.error, STD_ERROR_HANDLE, error);
  if (!duplicate(options_.input, STD_INPUT_HANDLE, input) ||
      !duplicate(options_.output, STD_OUTPUT_HANDLE, output)) { bridge.stop(); return false; }
  std::atomic<bool> stop{false}, failed{false}, read_done{false}, write_done{false};
  std::atomic<ULONGLONG> write_started{0};
  std::thread reader, writer;
  try {
    reader = std::thread([&] {
      try {
        std::string line;
        char buffer[4096];
        while (!stop) {
          DWORD count = 0;
          if (!ReadFile(input.get(), buffer, sizeof(buffer), &count, nullptr)) {
            if (!stop && GetLastError() != ERROR_BROKEN_PIPE) failed = true;
            break;
          }
          if (!count) break;
          for (DWORD i = 0; i < count && !stop; ++i) {
            if (buffer[i] == '\n') {
              if (!line.empty() && line.back() == '\r') line.pop_back();
              bridge.receive(line); line.clear();
              if (bridge.closed()) { failed = true; stop = true; break; }
            } else {
              if (line.size() >= options_.max_line_bytes) { failed = true; stop = true; break; }
              line += buffer[i];
            }
          }
        }
        // EOF with an unterminated frame is truncation, not another request.
        if (!line.empty() && !stop) failed = true;
      } catch (...) { failed = true; stop = true; }
      read_done = true;
    });
    writer = std::thread([&] {
      try {
        while (!stop) {
          auto line = bridge.takeOutput();
          if (!line) {
            if (read_done) break;
            std::this_thread::sleep_for(std::chrono::milliseconds{1}); continue;
          }
          write_started = GetTickCount64();
          std::size_t offset = 0;
          while (offset < line->size() && !stop) {
            DWORD count = 0;
            if (!WriteFile(output.get(), line->data() + offset,
                static_cast<DWORD>(line->size() - offset), &count, nullptr) || !count) {
              if (!stop) failed = true;
              stop = true; break;
            }
            offset += count;
          }
          write_started = 0;
        }
      } catch (...) { failed = true; stop = true; }
      write_done = true;
    });
  } catch (...) {
    failed = true; stop = true;
    if (!reader.joinable()) read_done = true;
    if (!writer.joinable()) write_done = true;
  }
  bool closed = false;
  while (!read_done || !write_done) {
    const auto began = write_started.load();
    if (began && GetTickCount64() - began >= static_cast<ULONGLONG>(options_.write_timeout.count())) {
      failed = true; stop = true;
    }
    if (!closed && bridge.closed()) { failed = true; stop = true; }
    if (!closed && (read_done || stop)) { bridge.stop(); closed = true; }
    // Repeat cancellation to cover stop racing the worker's check-before-I/O.
    // Join only after the synchronous call has returned and released its buffer.
    if (stop) {
      if (reader.joinable() && !read_done) CancelSynchronousIo(reader.native_handle());
      if (writer.joinable() && !write_done) CancelSynchronousIo(writer.native_handle());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  if (reader.joinable()) reader.join();
  if (writer.joinable()) writer.join();
  bridge.stop();
  if (failed && error) { try { diagnostic(error.get(), options_.write_timeout); } catch (...) {} }
  return !failed;
}
}  // namespace qingying::mcp
