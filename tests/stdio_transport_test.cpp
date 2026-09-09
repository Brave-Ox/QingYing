#include "qingying/mcp/stdio_transport.h"
#include "pipe_identity.h"
#include <gtest/gtest.h>
#include <future>
#include <thread>
#include <nlohmann/json.hpp>
namespace qingying::mcp {
namespace {
using namespace std::chrono_literals;
struct Pipes {
  ipc::detail::Handle input, send, output, receive, error, diagnostics;
  Pipes() {
    HANDLE read = nullptr, write = nullptr;
    if (!CreatePipe(&read, &write, nullptr, 4096)) throw std::runtime_error("test input pipe");
    input.reset(read); send.reset(write);
    if (!CreatePipe(&read, &write, nullptr, 4096)) throw std::runtime_error("test output pipe");
    receive.reset(read); output.reset(write);
    if (!CreatePipe(&read, &write, nullptr, 4096)) throw std::runtime_error("test error pipe");
    diagnostics.reset(read); error.reset(write);
  }
  StdioOptions options() {
    StdioOptions result;
    result.input = input.get(); result.output = output.get(); result.error = error.get();
    result.write_timeout = 100ms; return result;
  }
  void write(const std::string& text) {
    DWORD count = 0;
    ASSERT_TRUE(WriteFile(send.get(), text.data(), static_cast<DWORD>(text.size()), &count, nullptr));
    ASSERT_EQ(count, text.size());
  }
  std::string line(HANDLE handle) {
    std::string result;
    const auto end = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < end) {
      DWORD available = 0;
      if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) break;
      if (!available) { std::this_thread::sleep_for(1ms); continue; }
      char c = 0; DWORD count = 0;
      if (!ReadFile(handle, &c, 1, &count, nullptr) || !count) break;
      result += c;
      if (c == '\n') return result;
    }
    return result;
  }
};
}
TEST(StdioTransportTest, FragmentedInputAndCrLfProduceOnlyJsonOnStdout) {
  Pipes pipes; McpBridge bridge(nullptr); StdioTransport transport(pipes.options());
  auto run = std::async(std::launch::async, [&] { return transport.run(bridge); });
  pipes.write("{\"jsonrpc\":\"2.0\",\"id\":");
  pipes.write("18446744073709551615,\"method\":\"ping\"}\r\n");
  const auto line = pipes.line(pipes.receive.get());
  ASSERT_FALSE(line.empty());
  const auto response = nlohmann::json::parse(line);
  EXPECT_EQ(response["id"].get<std::uint64_t>(), UINT64_MAX);
  EXPECT_TRUE(response.contains("result"));
  pipes.send.reset(); ASSERT_EQ(run.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(run.get()); EXPECT_TRUE(bridge.closed());
  DWORD bytes = 0; ASSERT_TRUE(PeekNamedPipe(pipes.diagnostics.get(), nullptr, 0, nullptr, &bytes, nullptr));
  EXPECT_EQ(bytes, 0);
}
TEST(StdioTransportTest, TruncatedInputAndOversizeAreBoundedAndDiagnosticUsesStderr) {
  for (bool oversize : {false, true}) {
    Pipes pipes; auto options = pipes.options(); options.max_line_bytes = 128;
    McpBridge bridge(nullptr); StdioTransport transport(options);
    auto run = std::async(std::launch::async, [&] { return transport.run(bridge); });
    pipes.write(oversize ? std::string(256, 'x') : "{\"jsonrpc\":");
    pipes.send.reset(); ASSERT_EQ(run.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(run.get()); EXPECT_TRUE(bridge.closed());
    EXPECT_NE(pipes.line(pipes.diagnostics.get()).find("stdio transport failed"), std::string::npos);
    DWORD bytes = 0; ASSERT_TRUE(PeekNamedPipe(pipes.receive.get(), nullptr, 0, nullptr, &bytes, nullptr));
    EXPECT_EQ(bytes, 0);
  }
}
TEST(StdioTransportTest, SlowStdoutCancelsBlockedInputAndOutput) {
  Pipes pipes;
  AutomationLimits limits; limits.max_output_frames_per_connection = 64;
  McpBridge bridge(nullptr, limits); StdioTransport transport(pipes.options());
  auto run = std::async(std::launch::async, [&] { return transport.run(bridge); });
  // Long string IDs produce > pipe capacity while input remains open.
  for (int i = 0; i < 12; ++i)
    pipes.write(nlohmann::json{{"jsonrpc", "2.0"}, {"id", std::string(120, 'a') + std::to_string(i)},
        {"method", "unknown"}}.dump() + "\n");
  // Fill further without ever reading stdout.
  for (int i = 0; i < 12; ++i)
    pipes.write(nlohmann::json{{"jsonrpc", "2.0"}, {"id", std::string(120, 'b') + std::to_string(i)},
        {"method", "unknown"}}.dump() + "\n");
  // Capacity is deliberately above 24 replies: this exercises the blocked
  // synchronous WriteFile deadline, not the session queue-overflow path.
  EXPECT_EQ(run.wait_for(20ms), std::future_status::timeout);
  EXPECT_EQ(run.wait_for(3s), std::future_status::ready);
  EXPECT_FALSE(run.get());
}
TEST(StdioTransportTest, BrokenStdoutCancelsIdleRead) {
  Pipes pipes; McpBridge bridge(nullptr); StdioTransport transport(pipes.options());
  auto run = std::async(std::launch::async, [&] { return transport.run(bridge); });
  pipes.receive.reset();
  pipes.write("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}\n");
  EXPECT_EQ(run.wait_for(2s), std::future_status::ready);
  EXPECT_FALSE(run.get());
}
TEST(StdioTransportTest, InitializeListAndOfflineStatusShareInheritedStream) {
  Pipes pipes; McpBridge bridge(nullptr); StdioTransport transport(pipes.options());
  auto run = std::async(std::launch::async, [&] { return transport.run(bridge); });
  pipes.write(
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"stdio-fixture\",\"version\":\"1\"}}}\n"
      "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
      "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n"
      "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"status\"}}\n");
  EXPECT_EQ(nlohmann::json::parse(pipes.line(pipes.receive.get()))["result"]["protocolVersion"], "2025-11-25");
  EXPECT_EQ(nlohmann::json::parse(pipes.line(pipes.receive.get()))["result"]["tools"].size(), 9);
  EXPECT_EQ(nlohmann::json::parse(pipes.line(pipes.receive.get()))["result"]["structuredContent"]["reachable"], false);
  pipes.send.reset(); ASSERT_EQ(run.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(run.get()); EXPECT_EQ(bridge.clientInfo().name, "stdio-fixture");
}
}  // namespace qingying::mcp
