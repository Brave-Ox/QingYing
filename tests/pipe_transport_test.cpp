#include "qingying/ipc/pipe_automation_client.h"
#include "pipe_io.h"
#include <gtest/gtest.h>
#include <atomic>
#include <future>
#include <set>

namespace qingying::ipc {
namespace {
using namespace std::chrono_literals;
PipeOptions options() {
  static std::atomic<unsigned> serial{0};
  PipeOptions result;
  result.test_suffix = L"transport_" + std::to_wstring(GetCurrentProcessId()) +
      L"_" + std::to_wstring(GetTickCount64()) + L"_" + std::to_wstring(++serial);
  result.handshake_timeout = 1000ms;
  result.write_timeout = 250ms;
  return result;
}
struct Harness {
  explicit Harness(PipeOptions config = options()) : config(std::move(config)), server({
    [this]() -> std::optional<TrustedAutomationContext> {
      EXPECT_EQ(std::this_thread::get_id(), owner);
      if (reject) return std::nullopt;
      TrustedAutomationContext context;
      context.connection = {17, ++generation};
      context.action.result_scope = generation + 100;
      ++connected;
      return context;
    },
    [this](TrustedAutomationContext context, AutomationRequest request, AutomationCompletion completion) {
      EXPECT_EQ(std::this_thread::get_id(), owner);
      ++submitted;
      admission = context.submitted_at;
      AutomationResponse response;
      response.connection = context.connection;
      response.result.request_id = request.request_id;
      response.result.ok = true;
      response.result.error_code = ErrorCode::kOk;
      if (large) response.result.message.assign(48000, 'x');
      if (hold) held.push_back([completion, response] { completion(response); });
      else completion(std::move(response));
    },
    [this](const TrustedAutomationContext& context) {
      std::lock_guard<std::mutex> lock(mutex);
      revoked.insert(context.connection.generation);
    },
    [this](const TrustedAutomationContext& context) {
      EXPECT_EQ(std::this_thread::get_id(), owner);
      std::lock_guard<std::mutex> lock(mutex);
      EXPECT_EQ(revoked.count(context.connection.generation), 1);
      ++disconnected;
    }
  }, this->config) {}
  ~Harness() { server.stop(); }
  bool pump(const std::function<bool()>& ready, std::chrono::milliseconds timeout = 3000ms) {
    const auto deadline = detail::after(timeout);
    do {
      server.drain();
      if (ready()) return true;
      std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
  }
  bool connect(PipeAutomationClient& client) {
    auto future = std::async(std::launch::async, [&] { return client.connect(); });
    if (!pump([&] { return future.wait_for(0ms) == std::future_status::ready; })) return false;
    return future.get();
  }
  std::unique_ptr<detail::PipeStream> raw(DWORD sqos = SECURITY_IDENTIFICATION) {
    auto stream = std::make_unique<detail::PipeStream>(config);
    stream->pipe.reset(CreateFileW(server.name().c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | sqos, nullptr));
    if (!stream->pipe) return {};
    stream->startWriter();
    return stream;
  }
  bool hello(detail::PipeStream& stream) {
    if (!stream.send(WireHello{})) return false;
    auto future = std::async(std::launch::async, [&] { return stream.read(detail::after(1500ms)); });
    if (!pump([&] { return future.wait_for(0ms) == std::future_status::ready; })) return false;
    const auto result = future.get();
    return result && std::holds_alternative<WireHello>(*result);
  }
  PipeOptions config;
  std::thread::id owner{std::this_thread::get_id()};
  std::mutex mutex;
  std::set<ConnectionGeneration> revoked;
  unsigned connected{0}, submitted{0}, disconnected{0};
  ConnectionGeneration generation{0};
  bool reject{false}, hold{false}, large{false};
  detail::Deadline admission;
  std::vector<std::function<void()>> held;
  PipeServer server;
};
AutomationRequest request(RequestId id) {
  AutomationRequest result;
  result.request_id = id;
  return result;
}
WireRequest wire(RequestId id) {
  WireRequest result;
  result.rpc_id = id;
  result.request = request(id);
  return result;
}
}
TEST(PipeTransportTest, RoundTripWithOneByteReadsAndWrites) {
  auto config = options();
  config.io_chunk_bytes = 1;
  Harness test(config);
  ASSERT_TRUE(test.server.start()) << test.server.lastError();
  PipeAutomationClient client(config);
  ASSERT_TRUE(test.connect(client)) << client.lastError();
  EXPECT_EQ(client.connection().application_epoch, 17);
  EXPECT_EQ(client.connection().generation, 1);
  std::atomic<int> calls{0};
  client.submit(request(UINT64_MAX), [&](AutomationResponse response) {
    EXPECT_TRUE(response.result.ok);
    EXPECT_EQ(response.result.request_id, UINT64_MAX);
    EXPECT_TRUE(sameConnection(response.connection, client.connection()));
    ++calls;
  });
  ASSERT_TRUE(test.pump([&] { return calls == 1; }));
  client.close();
  EXPECT_TRUE(test.pump([&] { return test.disconnected == 1; }));
  EXPECT_FALSE(client.connect());
}
TEST(PipeTransportTest, CloseSettlesHeldRequestsOnceAndDropsLateCompletion) {
  Harness test;
  test.hold = true;
  ASSERT_TRUE(test.server.start());
  PipeAutomationClient client(test.config);
  ASSERT_TRUE(test.connect(client));
  std::atomic<int> calls{0};
  client.submit(request(1), [&](AutomationResponse response) {
    EXPECT_EQ(response.result.error_code, ErrorCode::kCancelled);
    EXPECT_EQ(response.connection.generation, 1);
    ++calls;
  });
  ASSERT_TRUE(test.pump([&] { return test.submitted == 1; }));
  client.close();
  EXPECT_EQ(calls, 1);
  ASSERT_TRUE(test.pump([&] { return test.disconnected == 1; }));
  test.held.front()();
  EXPECT_EQ(calls, 1);
}
TEST(PipeTransportTest, CloseFromCompletionIsSupported) {
  Harness test;
  ASSERT_TRUE(test.server.start());
  PipeAutomationClient client(test.config);
  ASSERT_TRUE(test.connect(client));
  std::atomic<int> calls{0};
  client.submit(request(1), [&](AutomationResponse) { client.close(); ++calls; });
  ASSERT_TRUE(test.pump([&] { return calls == 1; }));
  client.close();
}
TEST(PipeTransportTest, FourConnectionsAndFifthIsBounded) {
  Harness test;
  ASSERT_TRUE(test.server.start());
  std::vector<std::unique_ptr<PipeAutomationClient>> clients;
  for (int i = 0; i < 4; ++i) {
    auto client = std::make_unique<PipeAutomationClient>(test.config);
    ASSERT_TRUE(test.connect(*client));
    clients.push_back(std::move(client));
  }
  auto config = test.config;
  config.handshake_timeout = 60ms;
  PipeAutomationClient fifth(config);
  EXPECT_FALSE(fifth.connect());
  EXPECT_EQ(test.connected, 4);
  clients.front()->close();
  ASSERT_TRUE(test.pump([&] { return test.disconnected == 1; }));
  PipeAutomationClient replacement(test.config);
  ASSERT_TRUE(test.connect(replacement));
  EXPECT_EQ(replacement.connection().generation, 5);
}
TEST(PipeTransportTest, PreemptedEndpointFailsWithoutFallback) {
  Harness first;
  ASSERT_TRUE(first.server.start());
  Harness second(first.config);
  EXPECT_FALSE(second.server.start());
  EXPECT_EQ(second.server.lastError(), ERROR_ACCESS_DENIED);
  PipeAutomationClient client(first.config);
  EXPECT_TRUE(first.connect(client));
}
TEST(PipeTransportTest, HandshakeTimeoutReleasesUnauthenticatedSlot) {
  auto config = options();
  config.handshake_timeout = 80ms;
  config.limits.max_connections = 1;
  Harness test(config);
  ASSERT_TRUE(test.server.start());
  auto raw = test.raw();
  ASSERT_NE(raw, nullptr);
  auto read = std::async(std::launch::async, [&] { return raw->read(detail::after(1000ms)); });
  ASSERT_TRUE(test.pump([&] { return read.wait_for(0ms) == std::future_status::ready; }));
  EXPECT_FALSE(read.get());
  EXPECT_EQ(test.connected, 0);
  PipeAutomationClient client(config);
  EXPECT_TRUE(test.connect(client));
}
TEST(PipeTransportTest, PartialFrameAndImmediateDisconnectNeverCreateContext) {
  Harness test;
  ASSERT_TRUE(test.server.start());
  auto raw = test.raw();
  ASSERT_NE(raw, nullptr);
  char bytes[] = {20, 0};
  DWORD transferred = 0;
  ASSERT_TRUE(detail::pipeTransfer(raw->pipe.get(), raw->stopEvent(), true, bytes, 2,
      transferred, detail::after(1000ms)));
  raw.reset();
  auto immediate = test.raw();
  ASSERT_NE(immediate, nullptr);
  immediate.reset();
  PipeAutomationClient client(test.config);
  ASSERT_TRUE(test.connect(client));
  EXPECT_EQ(test.connected, 1);
}
TEST(PipeTransportTest, AnonymousEffectiveIdentityIsRejectedBeforeContext) {
  Harness test;
  ASSERT_TRUE(test.server.start());
  auto raw = test.raw(SECURITY_ANONYMOUS);
  ASSERT_NE(raw, nullptr);
  EXPECT_FALSE(test.hello(*raw));
  EXPECT_EQ(test.connected, 0);
  PipeAutomationClient client(test.config);
  EXPECT_TRUE(test.connect(client));
}
TEST(PipeTransportTest, WrongDirectionAndRepeatedHelloDisconnect) {
  Harness test;
  ASSERT_TRUE(test.server.start());
  auto raw = test.raw();
  ASSERT_NE(raw, nullptr);
  ASSERT_TRUE(raw->send(wire(1)));
  EXPECT_FALSE(raw->read(detail::after(1000ms)));
  EXPECT_EQ(test.connected, 0);
  auto good = test.raw();
  ASSERT_NE(good, nullptr);
  ASSERT_TRUE(test.hello(*good));
  ASSERT_TRUE(good->send(WireHello{}));
  ASSERT_TRUE(test.pump([&] { return test.disconnected == 1; }));
  EXPECT_EQ(test.submitted, 0);
}
TEST(PipeTransportTest, UiAdmissionRejectsWithoutServerHello) {
  Harness test;
  test.reject = true;
  ASSERT_TRUE(test.server.start());
  PipeAutomationClient client(test.config);
  EXPECT_FALSE(test.connect(client));
  EXPECT_EQ(test.connected, 0);
}
TEST(PipeTransportTest, DuplicateInflightIdDisconnectsAndLateCompletionIsIgnored) {
  Harness test;
  test.hold = true;
  ASSERT_TRUE(test.server.start());
  auto raw = test.raw();
  ASSERT_NE(raw, nullptr);
  ASSERT_TRUE(test.hello(*raw));
  ASSERT_TRUE(raw->send(wire(1)));
  ASSERT_TRUE(test.pump([&] { return test.submitted == 1; }));
  ASSERT_TRUE(raw->send(wire(1)));
  ASSERT_TRUE(test.pump([&] { return test.disconnected == 1; }));
  test.held.front()();
  EXPECT_EQ(test.submitted, 1);
}
TEST(PipeTransportTest, PendingRequestsAreBoundedAndControlsHaveSeparateCapacity) {
  auto config = options();
  config.limits.max_queued_requests_per_connection = 1;
  config.limits.max_control_requests_per_connection = 1;
  Harness test(config);
  test.hold = true;
  ASSERT_TRUE(test.server.start());
  auto raw = test.raw();
  ASSERT_NE(raw, nullptr);
  ASSERT_TRUE(test.hello(*raw));
  auto ordinary = wire(1);
  ordinary.request.payload = BeginLongShotRequest{};
  ASSERT_TRUE(raw->send(ordinary));
  ASSERT_TRUE(test.pump([&] { return test.submitted == 1; }));
  ASSERT_TRUE(raw->send(wire(2)));
  ASSERT_TRUE(test.pump([&] { return test.submitted == 2; }));
  ASSERT_TRUE(raw->send(wire(3)));
  ASSERT_TRUE(test.pump([&] { return test.disconnected == 1; }));
  EXPECT_EQ(test.submitted, 2);
}
TEST(PipeTransportTest, SlowReaderDoesNotBlockAnotherClientOrStop) {
  Harness test;
  test.large = true;
  ASSERT_TRUE(test.server.start());
  auto slow = test.raw();
  ASSERT_NE(slow, nullptr);
  ASSERT_TRUE(test.hello(*slow));
  ASSERT_TRUE(slow->send(wire(1)));
  ASSERT_TRUE(test.pump([&] { return test.submitted == 1; }));
  PipeAutomationClient fast(test.config);
  ASSERT_TRUE(test.connect(fast));
  std::atomic<int> calls{0};
  fast.submit(request(1), [&](AutomationResponse result) { EXPECT_TRUE(result.result.ok); ++calls; });
  ASSERT_TRUE(test.pump([&] { return calls == 1; }));
  const auto before = std::chrono::steady_clock::now();
  test.server.stop();
  EXPECT_LT(std::chrono::steady_clock::now() - before, 1500ms);
  EXPECT_EQ(test.disconnected, 2);
}
TEST(PipeTransportTest, QueuedAdmissionTimeIsPreservedAndDisconnectPreventsDispatch) {
  Harness test;
  ASSERT_TRUE(test.server.start());
  PipeAutomationClient client(test.config);
  ASSERT_TRUE(test.connect(client));
  const auto before = std::chrono::steady_clock::now();
  std::atomic<int> calls{0};
  client.submit(request(1), [&](AutomationResponse) { ++calls; });
  std::this_thread::sleep_for(30ms);
  const auto drain_time = std::chrono::steady_clock::now();
  ASSERT_TRUE(test.pump([&] { return calls == 1; }));
  EXPECT_GE(test.admission, before);
  EXPECT_LT(test.admission, drain_time);
  client.submit(request(2), [&](AutomationResponse) { ++calls; });
  client.close();
  const auto deadline = detail::after(1000ms);
  bool revoked = false;
  while (std::chrono::steady_clock::now() < deadline) {
    { std::lock_guard<std::mutex> lock(test.mutex); revoked = !test.revoked.empty(); }
    if (revoked) break;
    std::this_thread::sleep_for(1ms);
  }
  ASSERT_TRUE(revoked);
  test.server.drain();
  EXPECT_EQ(test.submitted, 1);
  EXPECT_EQ(calls, 2);
}
TEST(PipeTransportTest, StopCancelsPendingConnectAndReadWithoutUiPump) {
  Harness test;
  ASSERT_TRUE(test.server.start());
  auto raw = test.raw();
  ASSERT_NE(raw, nullptr);
  const auto before = std::chrono::steady_clock::now();
  test.server.stop();
  EXPECT_LT(std::chrono::steady_clock::now() - before, 1500ms);
}
TEST(PipeTransportTest, AlreadyConnectedAndCancelledReadCollectCompletion) {
  auto config = options();
  detail::Identity identity;
  ASSERT_TRUE(detail::currentIdentity(identity));
  detail::PipeSecurity security(identity);
  auto name = detail::pipeName(identity, config.test_suffix);
  detail::Handle server(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX |
      FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_REJECT_REMOTE_CLIENTS,
      1, 4096, 4096, 0, security.attributes()));
  ASSERT_TRUE(server);
  detail::Handle client(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
      nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
  ASSERT_TRUE(client);
  detail::Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  ASSERT_TRUE(detail::pipeConnect(server.get(), stop.get()));
  char buffer[16]{};
  auto future = std::async(std::launch::async, [&] {
    DWORD bytes = 0;
    return detail::pipeTransfer(server.get(), stop.get(), false, buffer, sizeof(buffer),
        bytes, detail::Deadline::max());
  });
  EXPECT_EQ(future.wait_for(20ms), std::future_status::timeout);
  SetEvent(stop.get());
  ASSERT_EQ(future.wait_for(1000ms), std::future_status::ready);
  EXPECT_FALSE(future.get());
  ResetEvent(stop.get());
  DWORD bytes = 0;
  char value = 'x';
  ASSERT_TRUE(detail::pipeTransfer(client.get(), stop.get(), true, &value, 1, bytes, detail::after(1000ms)));
  ASSERT_TRUE(detail::pipeTransfer(server.get(), stop.get(), false, buffer, sizeof(buffer), bytes, detail::after(1000ms)));
  EXPECT_EQ(bytes, 1);
  EXPECT_EQ(buffer[0], 'x');
  auto wrong = identity;
  ++wrong.session;
  EXPECT_FALSE(detail::verifyPeer(client.get(), false, wrong));
  EXPECT_TRUE(detail::verifyPeer(client.get(), false, identity));
}
TEST(PipeTransportTest, SlowWriteExpiresAndOnlyItsConnectionIsRevoked) {
  auto config = options();
  config.write_timeout = 60ms;
  Harness test(config);
  test.large = true;
  ASSERT_TRUE(test.server.start());
  auto slow = test.raw();
  ASSERT_NE(slow, nullptr);
  ASSERT_TRUE(test.hello(*slow));
  ASSERT_TRUE(slow->send(wire(1)));
  ASSERT_TRUE(test.pump([&] { return test.disconnected == 1; }));
  PipeAutomationClient good(config);
  EXPECT_TRUE(test.connect(good));
}
TEST(PipeTransportTest, OutputFrameBudgetIncludesActiveWrite) {
  auto config = options();
  config.limits.max_output_frames_per_connection = 1;
  config.write_timeout = 1000ms;
  Harness test(config);
  test.large = true;
  ASSERT_TRUE(test.server.start());
  auto slow = test.raw();
  ASSERT_NE(slow, nullptr);
  ASSERT_TRUE(test.hello(*slow));
  ASSERT_TRUE(slow->send(wire(1)));
  ASSERT_TRUE(test.pump([&] { return test.submitted == 1; }));
  ASSERT_TRUE(slow->send(wire(2)));
  ASSERT_TRUE(test.pump([&] { return test.disconnected == 1; }));
  EXPECT_EQ(test.submitted, 2);
}
TEST(PipeTransportTest, GlobalCapacityCoversRequestsAlreadyDispatchedToOwner) {
  auto config = options();
  config.limits.max_control_requests_per_connection = 1;
  config.limits.max_control_requests_global = 1;
  Harness test(config);
  test.hold = true;
  ASSERT_TRUE(test.server.start());
  auto first = test.raw();
  ASSERT_NE(first, nullptr);
  ASSERT_TRUE(test.hello(*first));
  auto second = test.raw();
  ASSERT_NE(second, nullptr);
  ASSERT_TRUE(test.hello(*second));
  ASSERT_TRUE(first->send(wire(1)));
  ASSERT_TRUE(test.pump([&] { return test.submitted == 1; }));
  ASSERT_TRUE(second->send(wire(1)));
  ASSERT_TRUE(test.pump([&] { return test.disconnected == 1; }));
  EXPECT_EQ(test.submitted, 1);
}
TEST(PipeTransportTest, ClientReservesControlLaneAndRejectsDuplicateOnce) {
  auto config = options();
  config.limits.max_queued_requests_per_connection = 1;
  config.limits.max_control_requests_per_connection = 1;
  Harness test(config);
  test.hold = true;
  ASSERT_TRUE(test.server.start());
  PipeAutomationClient client(config);
  ASSERT_TRUE(test.connect(client));
  std::atomic<int> cancelled{0}, rejected{0};
  auto callback = [&](AutomationResponse response) {
    if (response.result.error_code == ErrorCode::kCancelled) ++cancelled;
    else if (response.result.error_code == ErrorCode::kResourceLimit) ++rejected;
    else ADD_FAILURE();
  };
  auto ordinary = request(1);
  ordinary.payload = BeginLongShotRequest{};
  client.submit(ordinary, callback);
  ordinary.request_id = 2;
  client.submit(ordinary, callback);
  EXPECT_EQ(rejected, 1);
  client.submit(request(3), callback);
  client.submit(request(3), callback);
  EXPECT_EQ(rejected, 2);
  ASSERT_TRUE(test.pump([&] { return test.submitted == 2; }));
  client.close();
  EXPECT_EQ(cancelled, 2);
}
TEST(PipeTransportTest, DuplicateCompletionCannotConsumeReusedRequestId) {
  Harness test;
  test.hold = true;
  ASSERT_TRUE(test.server.start());
  PipeAutomationClient client(test.config);
  ASSERT_TRUE(test.connect(client));
  std::atomic<int> calls{0};
  client.submit(request(1), [&](AutomationResponse) { ++calls; });
  ASSERT_TRUE(test.pump([&] { return test.submitted == 1; }));
  test.held[0]();
  ASSERT_TRUE(test.pump([&] { return calls == 1; }));
  client.submit(request(1), [&](AutomationResponse) { ++calls; });
  ASSERT_TRUE(test.pump([&] { return test.submitted == 2; }));
  test.held[0]();
  test.held[1]();
  ASSERT_TRUE(test.pump([&] { return calls == 2; }));
}
TEST(PipeTransportTest, InvalidConfigurationAndClosedClientRejectSynchronously) {
  auto config = options();
  config.limits.max_connections = 5;
  EXPECT_THROW(PipeAutomationClient client(config), std::invalid_argument);
  PipeAutomationClient client(options());
  client.close();
  int calls = 0;
  client.submit(request(1), [&](AutomationResponse response) {
    EXPECT_EQ(response.result.error_code, ErrorCode::kCancelled);
    ++calls;
  });
  EXPECT_EQ(calls, 1);
  EXPECT_FALSE(client.connect());
}
TEST(PipeTransportTest, SingleSlotSurvivesRepeatedConnectThenImmediateClose) {
  auto config = options();
  config.limits.max_connections = 1;
  Harness test(config);
  ASSERT_TRUE(test.server.start());
  for (int i = 0; i < 5; ++i) {
    // An instance exists before its worker necessarily starts ConnectNamedPipe.
    std::unique_ptr<detail::PipeStream> raw;
    ASSERT_TRUE(test.pump([&] { raw = test.raw(); return raw != nullptr; }));
    raw.reset();
    PipeAutomationClient client(config);
    ASSERT_TRUE(test.connect(client));
    client.close();
    ASSERT_TRUE(test.pump([&] { return test.disconnected == static_cast<unsigned>(i + 1); }));
  }
}
}  // namespace qingying::ipc
