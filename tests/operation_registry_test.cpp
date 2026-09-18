#include "qingying/automation/operation_registry.h"
#include "qingying/automation/automation_contract.h"
#include "qingying/app/result_store.h"

#include <gtest/gtest.h>

#include <atomic>
#include <limits>
#include <thread>
#include <stdexcept>

namespace qingying {
namespace {
ActionResult success() {
  ActionResult result;
  result.ok = true;
  result.error_code = ErrorCode::kOk;
  return result;
}

AutomationRequest request(ActionPayload payload = StatusRequest{}, RequestId id = 1,
                          std::optional<std::string> key = std::nullopt) {
  AutomationRequest value;
  value.request_id = id;
  value.payload = ExecuteActionRequest{std::move(payload), std::move(key)};
  return value;
}

class OperationRegistryTest : public ::testing::Test {
 protected:
  using Clock = std::chrono::steady_clock;
  Clock::time_point now{};
  OperationRegistry registry{123, {}, [this] { return now; }};
  TrustedAutomationContext a = *registry.connect(2);
  TrustedAutomationContext b = *registry.connect(3);
};
}  // namespace

TEST_F(OperationRegistryTest, RecordsBeforeSynchronousCompletionAndSettlesOnce) {
  const auto submitted = registry.begin(a, request());
  ASSERT_TRUE(submitted.ok());
  EXPECT_EQ(registry.get(a, submitted.operation_id)->state, OperationState::Queued);
  EXPECT_TRUE(registry.complete(a, submitted.operation_id, success()));
  const auto snapshot = registry.get(a, submitted.handle);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->state, OperationState::Succeeded);
  ASSERT_TRUE(snapshot->outcome);
  EXPECT_EQ(snapshot->outcome->request_id, 1u);
  EXPECT_EQ(snapshot->outcome->operation_id, submitted.operation_id);
  EXPECT_FALSE(registry.complete(a, submitted.operation_id, success()));
}

TEST_F(OperationRegistryTest, InteractivePhasesAndDiagnosticsAreRetained) {
  AutomationRequest longshot;
  longshot.request_id = 1;
  longshot.payload = BeginLongShotRequest{};
  const auto submitted = registry.begin(a, longshot);
  ASSERT_TRUE(submitted.ok());
  EXPECT_TRUE(registry.advance(a, submitted.operation_id, OperationState::AwaitingUser));
  EXPECT_TRUE(registry.advance(a, submitted.operation_id, OperationState::Running));
  EXPECT_TRUE(registry.advance(a, submitted.operation_id, OperationState::Paused));
  EXPECT_TRUE(registry.advance(a, submitted.operation_id, OperationState::Running,
                               OperationProgress{"capturing", 7, 100, 200, {}}));
  EXPECT_FALSE(registry.advance(a, submitted.operation_id, OperationState::Succeeded));
  ActionResult failed;
  failed.error_code = ErrorCode::kCaptureFailed;
  failed.failure_stage = "stitch";
  failed.failure_frame = 7;
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, failed));
  const auto snapshot = registry.get(a, submitted.operation_id);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->progress.frames, 7u);
  EXPECT_EQ(snapshot->outcome->failure_stage, "stitch");
  EXPECT_EQ(snapshot->outcome->failure_frame, 7);
  EXPECT_EQ(snapshot->state, OperationState::Failed);
}

TEST_F(OperationRegistryTest, CancelBeforeCommitWaitsForExecutorAcknowledgement) {
  const auto submitted = registry.begin(a, request(CopyRequest{ResultSelection::specific(1)}));
  ASSERT_TRUE(submitted.ok());
  const auto cancelled = registry.cancel(a, submitted.operation_id);
  ASSERT_TRUE(cancelled);
  EXPECT_TRUE(cancelled->cancellation_requested);
  EXPECT_EQ(cancelled->state, OperationState::Cancelling);
  EXPECT_FALSE(registry.get(a, submitted.operation_id)->completed_at);
  EXPECT_FALSE(submitted.control->tryCommit());
  EXPECT_FALSE(registry.cancel(a, submitted.operation_id)->cancellation_requested);
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, success()));
  const auto snapshot = registry.get(a, submitted.operation_id);
  EXPECT_EQ(snapshot->state, OperationState::Cancelled);
  EXPECT_EQ(snapshot->abort_reason, AbortReason::ClientCancel);
  EXPECT_FALSE(snapshot->outcome->ok);
  EXPECT_FALSE(registry.cancel(a, submitted.operation_id)->cancellation_requested);
}

TEST_F(OperationRegistryTest, CommitBeforeCancelIsNotPrematureSuccessAndMayFail) {
  const auto submitted = registry.begin(a, request(SaveRequest{ResultSelection::specific(1), L"out.png"}));
  ASSERT_TRUE(submitted.ok());
  ASSERT_TRUE(submitted.control->tryCommit());
  EXPECT_FALSE(submitted.control->tryCommit());
  EXPECT_FALSE(registry.cancel(a, submitted.operation_id)->cancellation_requested);
  const auto running = registry.get(a, submitted.operation_id);
  EXPECT_EQ(running->state, OperationState::Finalizing);
  EXPECT_TRUE(running->committed);
  EXPECT_FALSE(running->outcome);
  now += std::chrono::minutes{2};
  ActionResult failure;
  failure.error_code = ErrorCode::kExportFailed;
  failure.failure_stage = "write";
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, failure));
  EXPECT_EQ(registry.get(a, submitted.operation_id)->state, OperationState::Failed);
  EXPECT_EQ(registry.get(a, submitted.operation_id)->outcome->error_code, ErrorCode::kExportFailed);
}

TEST_F(OperationRegistryTest, SuccessfulSideEffectRequiresCommitPermission) {
  const auto submitted = registry.begin(a, request(CopyRequest{ResultSelection::specific(1)}));
  ASSERT_TRUE(submitted.ok());
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, success()));
  EXPECT_EQ(registry.get(a, submitted.operation_id)->outcome->error_code, ErrorCode::kConflict);
}

TEST_F(OperationRegistryTest, DeadlineIncludesQueueTimeAndRequiresAcknowledgement) {
  auto value = request();
  value.timeout = std::chrono::milliseconds{10};
  const auto submitted = registry.begin(a, value);
  now += std::chrono::milliseconds{10};
  registry.sweep();
  const auto waiting = registry.get(a, submitted.operation_id);
  ASSERT_TRUE(waiting);
  EXPECT_EQ(waiting->state, OperationState::Cancelling);
  EXPECT_EQ(waiting->abort_reason, AbortReason::Deadline);
  EXPECT_FALSE(waiting->completed_at);
  EXPECT_FALSE(submitted.control->tryCommit());
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, success()));
  EXPECT_EQ(registry.get(a, submitted.operation_id)->state, OperationState::TimedOut);
}

TEST_F(OperationRegistryTest, SuppliedAdmissionControlIsReusedAndCannotBeClaimedTwice) {
  auto value = request();
  const auto timeout = automationTimeout(value, AutomationLimits{});
  auto control = std::make_shared<OperationControl>(a, value.request_id, timeout, [this] { return now; });
  EXPECT_TRUE(control->requestCancel(AbortReason::ClientCancel));
  const auto submitted = registry.begin(a, value, control);
  ASSERT_TRUE(submitted.ok());
  EXPECT_EQ(submitted.control.get(), control.get());
  EXPECT_EQ(registry.get(a, submitted.operation_id)->state, OperationState::Cancelling);
  EXPECT_EQ(registry.begin(a, value, control).error_code, ErrorCode::kConflict);
  EXPECT_EQ(registry.begin(b, value, control).error_code, ErrorCode::kConflict);
}

TEST_F(OperationRegistryTest, SameRpcIdAcrossConnectionsDoesNotShareOperations) {
  const auto first = registry.begin(a, request());
  const auto second = registry.begin(b, request());
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_NE(first.operation_id, second.operation_id);
  EXPECT_NE(first.handle.value, second.handle.value);
  EXPECT_FALSE(registry.get(b, first.operation_id));
  EXPECT_FALSE(registry.get(b, first.handle));
  EXPECT_FALSE(registry.cancel(b, first.operation_id));
  EXPECT_FALSE(registry.complete(b, first.operation_id, success()));
  auto wrong_scope = a;
  wrong_scope.action.result_scope = b.action.result_scope;
  EXPECT_FALSE(registry.get(wrong_scope, first.operation_id));
  EXPECT_EQ(registry.get(a, first.operation_id)->state, OperationState::Queued);
}

TEST_F(OperationRegistryTest, KeysReuseCanonicalBusinessParametersAcrossRpcIds) {
  const auto first = registry.begin(a, request(
      SaveRequest{ResultSelection::specific(7), L"C:\\shots\\.\\frame.png"}, 1, "save-key"));
  const auto again = registry.begin(a, request(
      SaveRequest{ResultSelection::specific(7), L"C:/shots/frame.png"}, 99, "save-key"));
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(again.ok());
  EXPECT_TRUE(again.reused);
  EXPECT_EQ(first.operation_id, again.operation_id);
  EXPECT_EQ(first.control.get(), again.control.get());
  EXPECT_EQ(registry.recordCount(), 1u);
  ASSERT_TRUE(first.control->tryCommit());
  ASSERT_TRUE(registry.complete(a, first.operation_id, success()));
  EXPECT_TRUE(registry.begin(a, request(
      SaveRequest{ResultSelection::specific(7), L"C:/shots/frame.png"}, 100, "save-key")).reused);
}

TEST_F(OperationRegistryTest, KeysConflictOnDifferentParametersAndAreConnectionLocal) {
  const auto first = registry.begin(a, request(CopyRequest{ResultSelection::specific(7)}, 1, "key"));
  ASSERT_TRUE(first.ok());
  EXPECT_EQ(registry.begin(a, request(CopyRequest{ResultSelection::specific(8)}, 2, "key")).error_code,
            ErrorCode::kConflict);
  EXPECT_EQ(registry.begin(a, request(PinRequest{ResultSelection::specific(7)}, 3, "key")).error_code,
            ErrorCode::kConflict);
  const auto other = registry.begin(b, request(CopyRequest{ResultSelection::specific(7)}, 1, "key"));
  EXPECT_TRUE(other.ok());
  EXPECT_FALSE(other.reused);
  EXPECT_NE(first.operation_id, other.operation_id);
  const auto unkeyed = registry.begin(a, request(CopyRequest{ResultSelection::specific(7)}, 1));
  EXPECT_TRUE(unkeyed.ok());
  EXPECT_FALSE(unkeyed.reused);
}

TEST_F(OperationRegistryTest, DisconnectDeniesQueriesUntilExecutorActuallyDrains) {
  const auto submitted = registry.begin(a, request());
  registry.disconnect(a);
  EXPECT_FALSE(registry.get(a, submitted.operation_id));
  EXPECT_FALSE(registry.cancel(a, submitted.operation_id));
  EXPECT_EQ(submitted.control->status().abort_reason, AbortReason::Disconnect);
  EXPECT_EQ(registry.recordCount(), 1u);
  EXPECT_FALSE(registry.connect(a.action.result_scope));
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, success()));
  EXPECT_EQ(registry.recordCount(), 0u);
  const auto reconnected = registry.connect(a.action.result_scope);
  ASSERT_TRUE(reconnected);
  EXPECT_NE(reconnected->connection.generation, a.connection.generation);
  EXPECT_FALSE(registry.get(*reconnected, submitted.handle));
  EXPECT_EQ(registry.begin(a, request()).error_code, ErrorCode::kAccessDenied);
}

TEST_F(OperationRegistryTest, ResultHandlesRejectCrossScopeReconnectAndRestart) {
  const auto handle = registry.bindResult(a, 71);
  ASSERT_TRUE(handle);
  EXPECT_EQ(registry.resolveResult(a, *handle), 71u);
  EXPECT_FALSE(registry.resolveResult(b, *handle));
  EXPECT_FALSE(registry.bindResult(b, 71));
  registry.disconnect(a);
  const auto reconnected = registry.connect(2);
  ASSERT_TRUE(reconnected);
  EXPECT_FALSE(registry.resolveResult(*reconnected, *handle));
  EXPECT_FALSE(registry.resolveResult(a, *handle));
  OperationRegistry restarted(456, {}, [this] { return now; });
  const auto fresh = restarted.connect(2);
  ASSERT_TRUE(fresh);
  EXPECT_FALSE(restarted.resolveResult(*fresh, *handle));
  EXPECT_EQ(restarted.begin(a, request()).error_code, ErrorCode::kAccessDenied);
}

TEST_F(OperationRegistryTest, InvalidatedResultHandlesOnlyResolveForControlWithinRetention) {
  const auto first = registry.bindResult(a, 1);
  ASSERT_TRUE(first);
  ASSERT_TRUE(registry.bindResult(a, 2));
  EXPECT_FALSE(registry.resolveResult(a, *first));
  EXPECT_EQ(registry.resolveResult(a, *first, true), 1u);
  EXPECT_FALSE(registry.resolveResult(b, *first, true));
  EXPECT_FALSE(registry.bindResult(a, 1));
  EXPECT_FALSE(registry.bindResult(b, 1));
  now += std::chrono::minutes{5};
  EXPECT_FALSE(registry.resolveResult(a, *first, true));
  registry.sweep();
  EXPECT_FALSE(registry.resolveResult(a, *first, true));
}

TEST_F(OperationRegistryTest, InvalidatedResultHandleCountIsBounded) {
  const auto first = *registry.bindResult(a, 1);
  for (ResultId id = 2; id <= 67; ++id) ASSERT_TRUE(registry.bindResult(a, id));
  EXPECT_FALSE(registry.resolveResult(a, first, true));
}

TEST_F(OperationRegistryTest, CapturedMetadataCannotBindAnotherConnectionsResult) {
  ASSERT_TRUE(registry.bindResult(b, 7));
  const auto submitted = registry.begin(a, request(CaptureRegionRequest{{0, 0, 1, 1}}));
  ASSERT_TRUE(submitted.control->tryCommit());
  ActionResult outcome = success();
  outcome.output = CapturedResult{7, 1, 1, {0, 0, 1, 1}};
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, outcome));
  EXPECT_EQ(registry.get(a, submitted.operation_id)->outcome->error_code, ErrorCode::kAccessDenied);
}

TEST_F(OperationRegistryTest, OldOperationHandlesRejectReconnectAndDifferentEpoch) {
  const auto submitted = registry.begin(a, request());
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, success()));
  registry.disconnect(a);
  const auto fresh = *registry.connect(2);
  EXPECT_FALSE(registry.get(fresh, submitted.handle));
  EXPECT_FALSE(registry.get(fresh, submitted.operation_id));
  OperationRegistry restarted(456, {}, [this] { return now; });
  const auto restart_context = *restarted.connect(2);
  const auto other = restarted.begin(restart_context, request());
  ASSERT_TRUE(other.ok());
  EXPECT_FALSE(restarted.get(restart_context, submitted.handle));
  EXPECT_FALSE(restarted.get(a, other.handle));
}

TEST_F(OperationRegistryTest, CommittedExecutionCanSucceedAfterDisconnectAcknowledgement) {
  const auto submitted = registry.begin(a, request(CopyRequest{ResultSelection::specific(1)}));
  ASSERT_TRUE(submitted.control->tryCommit());
  registry.disconnect(a);
  EXPECT_EQ(submitted.control->status().abort_reason, AbortReason::None);
  EXPECT_FALSE(submitted.control->status().settled);
  EXPECT_EQ(registry.recordCount(), 1u);
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, success()));
  EXPECT_TRUE(submitted.control->status().settled);
  EXPECT_EQ(registry.recordCount(), 0u);
}

TEST_F(OperationRegistryTest, GlobalActiveLimitAndConnectionsRemainBounded) {
  const auto c = *registry.connect(4);
  const auto d = *registry.connect(5);
  EXPECT_FALSE(registry.connect(6));
  EXPECT_FALSE(registry.connect(2));
  EXPECT_FALSE(registry.connect(kGuiResultScopeId));
  for (const auto& context : {a, b, c}) {
    for (int i = 0; i < 9; ++i) ASSERT_TRUE(registry.begin(context, request(StatusRequest{}, i + 1)).ok());
  }
  for (int i = 0; i < 6; ++i) ASSERT_TRUE(registry.begin(d, request(StatusRequest{}, i + 1)).ok());
  EXPECT_EQ(registry.begin(d, request()).error_code, ErrorCode::kResourceLimit);
  EXPECT_EQ(registry.recordCount(), 33u);
}

TEST_F(OperationRegistryTest, InvalidRequestDoesNotAllocateOperation) {
  EXPECT_EQ(registry.begin(a, request(StatusRequest{}, 0)).error_code, ErrorCode::kInvalidArgument);
  EXPECT_EQ(registry.begin(a, request(StatusRequest{}, 1, "key")).error_code, ErrorCode::kInvalidArgument);
  EXPECT_EQ(registry.begin(a, request(CopyRequest{})).error_code, ErrorCode::kInvalidArgument);
  EXPECT_EQ(registry.recordCount(), 0u);
}

TEST_F(OperationRegistryTest, CanonicalParameterStorageHasAByteBound) {
  const auto submitted = registry.begin(a, request(
      SaveRequest{ResultSelection::specific(1), std::wstring(20000, L'x')}, 1, "key"));
  EXPECT_EQ(submitted.error_code, ErrorCode::kResourceLimit);
  EXPECT_EQ(registry.recordCount(), 0u);
}

TEST_F(OperationRegistryTest, ResultExpiryDoesNotRewriteSucceededOperationOrHoldPixels) {
  ResultStore images;
  const auto id = images.publish(2, Image{1, 1, {42}});
  const auto submitted = registry.begin(a, request(CaptureRegionRequest{{0, 0, 1, 1}}));
  ASSERT_TRUE(submitted.ok());
  ASSERT_TRUE(submitted.control->tryCommit());
  ActionResult outcome = success();
  outcome.output = images.acquire(2, id).metadata();
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, outcome));
  const auto result_handle = registry.bindResult(a, id);
  ASSERT_TRUE(result_handle);
  images.clearScope(2);
  EXPECT_EQ(images.budgetSnapshot().retained_bytes, 0u);
  registry.invalidateResult(a, id, ResultAvailability::Expired);
  const auto snapshot = registry.get(a, submitted.operation_id);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->state, OperationState::Succeeded);
  EXPECT_TRUE(snapshot->outcome->ok);
  EXPECT_EQ(snapshot->result_availability, ResultAvailability::Expired);
  EXPECT_FALSE(registry.resolveResult(a, *result_handle));
  EXPECT_EQ(std::get<CapturedResult>(snapshot->outcome->output).result_id, id);
}

TEST_F(OperationRegistryTest, DefaultTerminalRetentionIs64PerConnectionAndFiveMinutes) {
  OperationSubmission first;
  for (int i = 0; i < 65; ++i) {
    const auto submitted = registry.begin(a, request(StatusRequest{}, i + 1));
    ASSERT_TRUE(submitted.ok());
    if (i == 0) first = submitted;
    ASSERT_TRUE(registry.complete(a, submitted.operation_id, success()));
    now += std::chrono::milliseconds{1};
  }
  EXPECT_EQ(registry.recordCount(), 64u);
  EXPECT_FALSE(registry.get(a, first.handle));
  b.submitted_at = now;
  const auto other = registry.begin(b, request());
  ASSERT_TRUE(other.ok());
  ASSERT_TRUE(registry.complete(b, other.operation_id, success()));
  EXPECT_EQ(registry.recordCount(), 65u);
  now += std::chrono::minutes{5};
  registry.sweep();
  EXPECT_EQ(registry.recordCount(), 0u);
}

TEST_F(OperationRegistryTest, PruningForgetsKeysButDoesNotAutomaticallyReplay) {
  auto value = request(CopyRequest{ResultSelection::specific(7)}, 1, "key");
  const auto submitted = registry.begin(a, value);
  ASSERT_TRUE(submitted.control->tryCommit());
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, success()));
  now += std::chrono::minutes{5};
  registry.sweep();
  EXPECT_EQ(registry.recordCount(), 0u);
  a.submitted_at = now;
  const auto fresh = registry.begin(a, value);
  ASSERT_TRUE(fresh.ok());
  EXPECT_FALSE(fresh.reused);
  EXPECT_NE(fresh.operation_id, submitted.operation_id);
  EXPECT_EQ(registry.get(a, fresh.operation_id)->state, OperationState::Queued);
}

TEST_F(OperationRegistryTest, ActiveRecordsAndMetadataAreBounded) {
  for (int i = 0; i < 9; ++i) ASSERT_TRUE(registry.begin(a, request(StatusRequest{}, i + 1)).ok());
  EXPECT_EQ(registry.begin(a, request()).error_code, ErrorCode::kResourceLimit);
  EXPECT_TRUE(registry.begin(b, request()).ok());
  auto oversized = request(CopyRequest{ResultSelection::specific(1)}, 1, std::string(129, 'k'));
  EXPECT_EQ(registry.begin(b, oversized).error_code, ErrorCode::kResourceLimit);
  const auto submitted = registry.begin(b, request());
  ASSERT_TRUE(submitted.ok());
  OperationProgress progress;
  progress.stage.assign(65537, 'x');
  EXPECT_FALSE(registry.advance(b, submitted.operation_id, OperationState::Running, progress));
  ActionResult outcome = success();
  outcome.data.reserve(131072);
  ASSERT_TRUE(registry.complete(b, submitted.operation_id, std::move(outcome)));
  EXPECT_EQ(registry.get(b, submitted.operation_id)->outcome->error_code, ErrorCode::kResourceLimit);
}

TEST_F(OperationRegistryTest, OnlyControlMayBeUsedOffTheUiThread) {
  const auto submitted = registry.begin(a, request());
  std::atomic_bool rejected{false};
  std::thread io([&] {
    try { (void)registry.get(a, submitted.operation_id); }
    catch (const std::logic_error&) { rejected = true; }
    submitted.control->requestCancel(AbortReason::ClientCancel);
  });
  io.join();
  EXPECT_TRUE(rejected);
  EXPECT_EQ(registry.get(a, submitted.operation_id)->state, OperationState::Cancelling);
}

TEST_F(OperationRegistryTest, ConcurrentCancelAndCommitHaveExactlyOneWinner) {
  const auto submitted = registry.begin(a, request());
  std::atomic_bool start{false};
  bool cancelled = false, committed = false;
  std::thread cancel_thread([&] {
    while (!start.load()) std::this_thread::yield();
    cancelled = submitted.control->requestCancel(AbortReason::ClientCancel);
  });
  std::thread commit_thread([&] {
    while (!start.load()) std::this_thread::yield();
    committed = submitted.control->tryCommit();
  });
  start = true;
  cancel_thread.join();
  commit_thread.join();
  EXPECT_NE(cancelled, committed);
  EXPECT_FALSE(registry.get(a, submitted.operation_id)->completed_at);
  ASSERT_TRUE(registry.complete(a, submitted.operation_id, success()));
  EXPECT_EQ(registry.get(a, submitted.operation_id)->state,
            committed ? OperationState::Succeeded : OperationState::Cancelled);
}

TEST(OperationControlTest, DestructionCancelsOutstandingControlsWithoutSettlingThem) {
  std::shared_ptr<IOperationControl> control;
  {
    OperationRegistry registry(123);
    const auto context = *registry.connect(2);
    control = registry.begin(context, request()).control;
  }
  ASSERT_NE(control, nullptr);
  EXPECT_EQ(control->status().abort_reason, AbortReason::Shutdown);
  EXPECT_FALSE(control->status().settled);
  EXPECT_FALSE(control->tryCommit());
}

TEST(OperationControlTest, DeadlineArithmeticSaturatesAndInvalidTimeoutIsRejected) {
  TrustedAutomationContext context;
  context.connection = {1, 1};
  context.action.result_scope = 2;
  context.submitted_at = (std::chrono::steady_clock::time_point::max)() - std::chrono::seconds{1};
  auto now = context.submitted_at;
  OperationControl control(context, 1, std::chrono::seconds{30}, [&] { return now; });
  now = (std::chrono::steady_clock::time_point::max)();
  EXPECT_FALSE(control.tryCommit());
  EXPECT_EQ(control.status().abort_reason, AbortReason::Deadline);
  EXPECT_THROW(OperationControl(context, 1, (std::chrono::milliseconds::min)()), std::invalid_argument);
}

}  // namespace qingying
