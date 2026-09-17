#include "qingying/automation/ui_action_scheduler.h"
#include <gtest/gtest.h>
#include <thread>

namespace qingying {
namespace {
class UiActionSchedulerTest : public ::testing::Test {
 protected:
  std::chrono::steady_clock::time_point now{};
  OperationRegistry registry{123, {}, [this] { return now; }};
  TrustedAutomationContext context = *registry.connect(2);
  std::vector<std::pair<UINT, UiMessageToken>> messages;
  bool post_ok{true};
  int executions{0};
  int completions{0};
  int last_error{0};
  UiMessageToken running{0};
  std::shared_ptr<OperationControl> control;
  UiActionScheduler scheduler{
    [this](UINT message, UiMessageToken token) {
      if (post_ok) messages.emplace_back(message, token);
      return post_ok;
    },
    [this](UiMessageToken token, const TrustedAutomationContext& ctx,
           const AutomationRequest& request, std::shared_ptr<OperationControl> value) {
      EXPECT_EQ(std::this_thread::get_id(), ui_thread);
      ++executions;
      running = token;
      control = value;
      if (!std::holds_alternative<ExecuteActionRequest>(request.payload) &&
          !std::holds_alternative<BeginLongShotRequest>(request.payload)) return;
      const auto op = registry.begin(ctx, request, value);
      EXPECT_TRUE(op.ok());
      EXPECT_EQ(op.control, value);
    }, {}, [this] { return now; }};
  std::thread::id ui_thread = std::this_thread::get_id();
  void SetUp() override { ASSERT_TRUE(scheduler.connect(context)); }
  void TearDown() override { scheduler.shutdown(); }
  void submit(RequestId id, bool ordinary = true) {
    AutomationRequest request;
    request.request_id = id;
    if (ordinary) request.payload = BeginLongShotRequest{};
    scheduler.submit(context, request, [this](AutomationResponse response) {
      ++completions;
      last_error = response.result.error_code;
      EXPECT_EQ(response.connection.generation, context.connection.generation);
      (void)scheduler.pending(); // completion must be outside lock
    });
  }
  void pump() {
    auto batch = std::move(messages);
    messages.clear();
    for (auto message : batch) scheduler.dispatch(message.first, message.second);
  }
};
TEST_F(UiActionSchedulerTest, FullOrdinaryLaneStillAdmitsControlAndCancellation) {
  for (int i = 1; i <= 8; ++i) submit(i);
  submit(9);
  EXPECT_EQ(last_error, ErrorCode::kBusy);
  submit(10, false);
  EXPECT_EQ(scheduler.pending(), 9u);
  EXPECT_TRUE(scheduler.cancel(context, 1));
  pump();
  EXPECT_EQ(executions, 8);
  EXPECT_EQ(completions, 2);
  for (int i = 11; i <= 13; ++i) submit(i, false);
  submit(14, false);
  EXPECT_EQ(last_error, ErrorCode::kBusy);
}
TEST_F(UiActionSchedulerTest, FailedPostReturnsReservationImmediately) {
  post_ok = false;
  submit(1);
  EXPECT_EQ(completions, 1);
  EXPECT_EQ(scheduler.pending(), 0u);
  post_ok = true;
  submit(1);
  pump();
  EXPECT_EQ(executions, 1);
}
TEST_F(UiActionSchedulerTest, ExpiredAndDuplicateTokensNeverExecute) {
  submit(1);
  const auto message = messages.front();
  now += std::chrono::minutes{4};
  pump();
  scheduler.dispatch(message.first, message.second);
  EXPECT_EQ(executions, 0);
  EXPECT_EQ(completions, 1);
  EXPECT_EQ(last_error, ErrorCode::kTimeout);
}
TEST_F(UiActionSchedulerTest, WorkerCompletionReturnsToUiAndSettlesOnce) {
  std::thread submitter([this] { submit(1); });
  submitter.join();
  pump();
  ASSERT_TRUE(control);
  std::thread worker([this] {
    AutomationResponse response;
    response.result.ok = true;
    response.result.error_code = ErrorCode::kOk;
    EXPECT_TRUE(scheduler.complete(running, response));
    EXPECT_FALSE(scheduler.complete(running, response));
    EXPECT_THROW(scheduler.drain(), std::logic_error);
  });
  worker.join();
  EXPECT_EQ(completions, 0);
  const auto message = messages.front();
  pump();
  scheduler.dispatch(message.first, message.second);
  EXPECT_EQ(completions, 1);
  EXPECT_EQ(last_error, ErrorCode::kOk);
  EXPECT_FALSE(scheduler.cancel(context, 1));
}
TEST_F(UiActionSchedulerTest, CompletionPostFailureIsRecoveredByDrain) {
  submit(1);
  pump();
  post_ok = false;
  EXPECT_TRUE(scheduler.complete(running, {}));
  scheduler.drain();
  EXPECT_EQ(completions, 1);
  EXPECT_EQ(scheduler.pending(), 0u);
}
TEST_F(UiActionSchedulerTest, BurstUsesOneWakeAndBatchDrainWithoutTimer) {
  for (RequestId id = 1; id <= 8; ++id) submit(id);
  ASSERT_EQ(messages.size(), 1u);
  EXPECT_EQ(messages.front().first, WM_QINGYING_AUTOMATION_WAKE);
  pump();
  EXPECT_EQ(executions, 8);
  EXPECT_EQ(scheduler.queueUsage().running, 8u);
}
TEST_F(UiActionSchedulerTest, HousekeepingRecoveryRetiresLostWake) {
  submit(1);
  const auto stale = messages.front();
  messages.clear(); // simulate a successfully posted message being lost
  scheduler.drain();
  EXPECT_EQ(executions, 1);
  EXPECT_TRUE(scheduler.complete(running, {}));
  ASSERT_EQ(messages.size(), 1u);
  scheduler.dispatch(stale.first, stale.second);
  EXPECT_EQ(completions, 0);
  pump();
  EXPECT_EQ(completions, 1);
}
TEST_F(UiActionSchedulerTest, CancelCompletionAndShutdownSequenceSettlesOnce) {
  submit(1);
  pump();
  EXPECT_TRUE(scheduler.cancel(context, 1));
  EXPECT_TRUE(scheduler.complete(running, {}));
  ASSERT_EQ(messages.size(), 1u);
  const auto stale = messages.front();
  pump();
  EXPECT_EQ(completions, 1);
  scheduler.shutdown();
  scheduler.dispatch(stale.first, stale.second);
  EXPECT_FALSE(scheduler.notify());
  EXPECT_FALSE(scheduler.complete(running, {}));
  EXPECT_EQ(completions, 1);
}
TEST_F(UiActionSchedulerTest, NestedDrainDoesNotDispatchNewRequestsReentrantly) {
  bool nested = false;
  scheduler.setSettlementHooks([&](UiMessageToken, AutomationResponse&) {
    submit(2);
    scheduler.drain();
    EXPECT_EQ(executions, 1);
    nested = true;
  }, {});
  submit(1);
  pump();
  EXPECT_TRUE(scheduler.complete(running, {}));
  pump();
  EXPECT_TRUE(nested);
  scheduler.setSettlementHooks({}, {});
  pump();
  EXPECT_EQ(executions, 2);
}
TEST_F(UiActionSchedulerTest, DisconnectRejectsDecodedRequestsAndOldGeneration) {
  submit(1);
  scheduler.disconnect(context);
  std::thread decoder([this] { submit(2); });
  decoder.join();
  pump();
  scheduler.drain();
  EXPECT_EQ(executions, 0);
  EXPECT_EQ(completions, 2);
  EXPECT_FALSE(scheduler.connect(context));
}
TEST_F(UiActionSchedulerTest, WrongScopeCannotSubmitCancelOrDisconnect) {
  submit(1);
  auto wrong = context;
  wrong.action.result_scope = 3;
  EXPECT_FALSE(scheduler.cancel(wrong, 1));
  scheduler.disconnect(wrong);
  pump();
  EXPECT_EQ(executions, 1);
  EXPECT_EQ(control->status().abort_reason, AbortReason::None);
}
TEST_F(UiActionSchedulerTest, ShutdownSettlesRunningQueuedAndLateCompletions) {
  submit(1);
  pump();
  submit(2);
  scheduler.shutdown();
  EXPECT_EQ(completions, 2);
  EXPECT_EQ(scheduler.pending(), 0u);
  EXPECT_EQ(control->status().abort_reason, AbortReason::Shutdown);
  EXPECT_FALSE(scheduler.complete(running, {}));
  pump();
  submit(3);
  EXPECT_EQ(completions, 3);
  scheduler.shutdown();
}
}  // namespace
}  // namespace qingying

namespace qingying {
TEST(UiActionSchedulerLimits, GlobalCapacityAndGenerationIsolation) {
  OperationRegistry registry{99};
  std::vector<TrustedAutomationContext> contexts;
  std::size_t rejected = 0;
  UiActionScheduler scheduler{[](UINT, UiMessageToken) { return true; },
      [](UiMessageToken, const TrustedAutomationContext&, const AutomationRequest&,
         std::shared_ptr<OperationControl>) {}};
  for (int connection = 0; connection < 4; ++connection) {
    auto context = *registry.connect(2 + connection);
    ASSERT_TRUE(scheduler.connect(context));
    contexts.push_back(context);
    for (int id = 1; id <= 8; ++id) {
      AutomationRequest request;
      request.request_id = id;
      request.payload = BeginLongShotRequest{};
      scheduler.submit(context, request, [&](AutomationResponse response) {
        if (response.result.error_code == ErrorCode::kBusy) ++rejected;
      });
    }
  }
  EXPECT_EQ(scheduler.pending(), 32u);
  AutomationRequest request;
  request.request_id = 9;
  request.payload = BeginLongShotRequest{};
  scheduler.submit(contexts.front(), request, [&](AutomationResponse response) {
    if (response.result.error_code == ErrorCode::kBusy) ++rejected;
  });
  EXPECT_EQ(rejected, 1u);
  scheduler.shutdown();
  EXPECT_EQ(scheduler.pending(), 0u);
}
} // namespace qingying
namespace qingying {
TEST_F(UiActionSchedulerTest, PrivateCancelReachesRequestBeforeOperationAllocation) {
  submit(1);
  AutomationRequest request;
  request.request_id = 2;
  request.payload = CancelOperationRequest{RequestCancellation{1}};
  scheduler.submit(context, request, {});
  const auto token = messages.front().second;
  scheduler.dispatch(WM_QINGYING_AUTOMATION_COMPLETE, token);
  pump();
  EXPECT_EQ(executions, 1);
  EXPECT_EQ(completions, 1);
  EXPECT_EQ(last_error, ErrorCode::kCancelled);
}
TEST_F(UiActionSchedulerTest, ConcurrentDecodeAndDisconnectLeavesNoUnclaimedRequest) {
  for (int i = 0; i < 50; ++i) {
    OperationRegistry local_registry{static_cast<ApplicationEpoch>(i + 1)};
    auto ctx = *local_registry.connect(2);
    int done = 0;
    UiActionScheduler local{[](UINT, UiMessageToken) { return true; },
        [](UiMessageToken, const TrustedAutomationContext&, const AutomationRequest&,
           std::shared_ptr<OperationControl>) { FAIL() << "disconnected work executed"; }};
    ASSERT_TRUE(local.connect(ctx));
    std::thread decoder([&] {
      AutomationRequest request;
      request.request_id = 1;
      local.submit(ctx, request, [&](AutomationResponse) { ++done; });
    });
    local.disconnect(ctx);
    decoder.join();
    local.drain();
    EXPECT_EQ(done, 1);
    EXPECT_EQ(local.pending(), 0u);
  }
}
} // namespace qingying
