#include "qingying/automation/operation_registry.h"
#include <gtest/gtest.h>
namespace qingying {
namespace {
TrustedAutomationContext identity(ConnectionGeneration generation, ResultScopeId scope) {
  TrustedAutomationContext context;
  context.connection = {123, generation};
  context.action.result_scope = scope;
  return context;
}
AutomationRequest keyedCopy(RequestId id = 1) {
  AutomationRequest request;
  request.request_id = id;
  request.payload = ExecuteActionRequest{CopyRequest{ResultSelection::specific(99)}, "same-key"};
  return request;
}
}
TEST(AutomationSessionStateTest, TwoSessionsEnforceIdentityScopeAndIdempotentClose) {
  auto a = identity(1, 2), b = identity(2, 3);
  AutomationSessionState first{a, AutomationPeerIdentity{11}};
  AutomationSessionState second{b, AutomationPeerIdentity{22}};
  EXPECT_TRUE(first.accepts(a));
  EXPECT_FALSE(first.accepts(b));
  auto forged = a;
  forged.action.result_scope = b.action.result_scope;
  EXPECT_FALSE(first.accepts(forged));
  forged = a;
  ++forged.connection.application_epoch;
  EXPECT_FALSE(first.accepts(forged));
  EXPECT_FALSE(first.close(AutomationSessionCloseReason::None));
  EXPECT_TRUE(first.close(AutomationSessionCloseReason::Disconnected));
  EXPECT_FALSE(first.close(AutomationSessionCloseReason::Shutdown));
  EXPECT_EQ(first.closeReason(), AutomationSessionCloseReason::Disconnected);
  EXPECT_EQ(first.phase(), AutomationSessionPhase::Closed);
  EXPECT_TRUE(second.accepts(b));
  EXPECT_EQ(second.phase(), AutomationSessionPhase::Active);
  EXPECT_EQ(second.peer().process_id, 22u);
}
TEST(AutomationSessionStateTest, DisconnectOnlyRevokesItsOperationsKeysAndResultHandles) {
  OperationRegistry registry{123};
  auto a = *registry.connect(2), b = *registry.connect(3);
  a.submitted_at = b.submitted_at = std::chrono::steady_clock::now();
  const auto request = keyedCopy();
  auto first = registry.begin(a, request), second = registry.begin(b, request);
  ASSERT_TRUE(first.ok()); ASSERT_TRUE(second.ok());
  EXPECT_NE(first.operation_id, second.operation_id);
  auto ah = *registry.bindResult(a, 101), bh = *registry.bindResult(b, 102);
  EXPECT_FALSE(registry.resolveResult(b, ah));
  auto forged = a;
  forged.action.result_scope = b.action.result_scope;
  registry.disconnect(forged);
  EXPECT_TRUE(registry.isConnected(a));
  registry.disconnect(a);
  auto closing = registry.sessionSnapshot(a);
  ASSERT_TRUE(closing);
  EXPECT_EQ(closing->phase(), AutomationSessionPhase::Closing);
  EXPECT_EQ(closing->pendingOperationCount(), 1u);
  EXPECT_EQ(closing->idempotencyEntryCount(), 0u);
  EXPECT_FALSE(closing->ownedResultHandle());
  registry.disconnect(a, AutomationSessionCloseReason::Shutdown);
  EXPECT_EQ(registry.sessionSnapshot(a)->closeReason(), AutomationSessionCloseReason::Disconnected);
  EXPECT_TRUE(registry.isConnected(b));
  EXPECT_EQ(registry.resolveResult(b, bh), 102u);
  EXPECT_EQ(registry.sessionSnapshot(b)->idempotencyEntryCount(), 1u);
  EXPECT_EQ(second.control->status().abort_reason, AbortReason::None);
  ActionResult outcome;
  outcome.ok = true;
  outcome.error_code = ErrorCode::kOk;
  EXPECT_TRUE(registry.complete(a, first.operation_id, outcome));
  EXPECT_FALSE(registry.complete(a, first.operation_id, outcome));
  EXPECT_FALSE(registry.sessionSnapshot(a));
  EXPECT_TRUE(second.control->tryCommit());
  EXPECT_TRUE(registry.complete(b, second.operation_id, outcome));
  EXPECT_EQ(registry.sessionSnapshot(b)->pendingOperationCount(), 0u);
  EXPECT_TRUE(registry.begin(b, keyedCopy(2)).reused);
}
TEST(AutomationSessionStateTest, ExpiredOperationRemovesOnlyItsSessionIdempotencyIndex) {
  auto now = std::chrono::steady_clock::now();
  AutomationLimits limits;
  OperationRegistry registry{123, limits, [&] { return now; }};
  auto a = *registry.connect(2);
  auto first = registry.begin(a, keyedCopy());
  ASSERT_TRUE(first.ok());
  EXPECT_TRUE(first.control->tryCommit());
  ActionResult outcome;
  outcome.ok = true;
  outcome.error_code = ErrorCode::kOk;
  ASSERT_TRUE(registry.complete(a, first.operation_id, outcome));
  EXPECT_EQ(registry.sessionSnapshot(a)->idempotencyEntryCount(), 1u);
  now += limits.completed_operation_ttl + std::chrono::milliseconds{1};
  registry.sweep();
  EXPECT_EQ(registry.sessionSnapshot(a)->idempotencyEntryCount(), 0u);
  a.submitted_at = now;
  auto next = registry.begin(a, keyedCopy(2));
  ASSERT_TRUE(next.ok());
  EXPECT_FALSE(next.reused);
  EXPECT_NE(next.operation_id, first.operation_id);
}
}  // namespace qingying
