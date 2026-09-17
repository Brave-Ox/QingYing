#include "qingying/automation/automation_endpoint.h"
#include <gtest/gtest.h>
#include <algorithm>

namespace qingying {
namespace {
class FakePort final : public AutomationOperationPort, public AutomationResultPort {
 public:
  std::optional<AutomationActionDescriptor> describe(ActionType type) const override {
    if (type == ActionType::Copy) return AutomationActionDescriptor{type, "custom_copy", true};
    return std::nullopt;
  }
  void submit(const ActionRequest& request, ActionCompletion callback) override {
    pending = request;
    completion = std::move(callback);
    ++calls;
  }
  void finish() {
    ActionResult result;
    result.ok = pending.operation_control->tryCommit();
    result.error_code = result.ok ? ErrorCode::kOk : ErrorCode::kCancelled;
    auto callback = std::move(completion);
    callback(std::move(result));
  }
  void shutdown() override { ++stops; if (completion) finish(); }
  AutomationResultUsage budgetSnapshot() const noexcept override { return {12, 4}; }
  int resultStatus(ResultScopeId, ResultId) const noexcept override { return ErrorCode::kResultNotFound; }
  int releaseResult(ResultScopeId, ResultId) noexcept override { return ErrorCode::kResultNotFound; }
  void clearScope(ResultScopeId scope) noexcept override { cleared = scope; }
  void clearAll() noexcept override { ++clears; }
  void sweep() noexcept override {}
  ActionRequest pending;
  ActionCompletion completion;
  int calls{0}, clears{0}, stops{0};
  ResultScopeId cleared{0};
};
class AutomationCoreTest : public ::testing::Test {
 protected:
  FakePort port;
  InteractionGate gate;
  OperationRegistry registry{123};
  std::vector<std::pair<UINT, UiMessageToken>> messages;
  std::map<RequestId, AutomationResponse> responses;
  UiActionScheduler scheduler{
      [this](UINT message, UiMessageToken token) { messages.emplace_back(message, token); return true; },
      [this](UiMessageToken token, const TrustedAutomationContext& context,
          const AutomationRequest& request, std::shared_ptr<OperationControl> control) {
        endpoint.execute(token, context, request, std::move(control));
      }};
  AutomationEndpoint endpoint{port, port, registry, scheduler, gate, {},
      AutomationEndpoint::ExecutionPolicy{{ActionType::Copy}, {}, true}};
  TrustedAutomationContext context;
  void SetUp() override { context = *endpoint.connectAuthenticated(); }
  void submit(RequestId id, AutomationPayload payload) {
    AutomationRequest request;
    request.request_id = id;
    request.payload = std::move(payload);
    auto admitted = context;
    admitted.submitted_at = std::chrono::steady_clock::now();
    scheduler.submit(admitted, request, [this](AutomationResponse response) {
      responses.emplace(response.result.request_id, std::move(response));
    });
  }
  void pump() {
    while (!messages.empty()) {
      auto batch = std::move(messages); messages.clear();
      for (const auto& item : batch) scheduler.dispatch(item.first, item.second);
    }
  }
  ExecuteActionRequest copy() { return {CopyRequest{ResultSelection::specific(99)}, "same-copy"}; }
};
TEST_F(AutomationCoreTest, DescriptorControlsCapabilitiesAndAdmissionWithoutWorkflow) {
  const auto status = endpoint.status();
  EXPECT_NE(std::find(status.capabilities.begin(), status.capabilities.end(), "custom_copy"), status.capabilities.end());
  EXPECT_EQ(status.resources->result_bytes, 12u);
  auto dialog = gate.acquire(InteractionKind::SaveDialog);
  submit(1, copy()); pump();
  EXPECT_EQ(port.calls, 1);
  port.finish(); pump();
  EXPECT_TRUE(responses.at(1).result.ok);
}
TEST_F(AutomationCoreTest, IdempotencySharesExecutionAndReplaysOutcome) {
  submit(1, copy()); pump();
  submit(2, copy()); pump();
  EXPECT_EQ(port.calls, 1);
  EXPECT_TRUE(responses.empty());
  port.finish(); pump();
  ASSERT_TRUE(responses.at(1).result.ok);
  EXPECT_EQ(responses.at(1).result.operation_id, responses.at(2).result.operation_id);
  submit(3, copy()); pump();
  EXPECT_TRUE(responses.at(3).result.ok);
  EXPECT_EQ(port.calls, 1);
}
TEST_F(AutomationCoreTest, CancellationDisconnectAndShutdownPreserveSessionIsolation) {
  const auto second = *endpoint.connectAuthenticated();
  submit(1, copy()); pump();
  const auto operation = port.pending.operation_id;
  EXPECT_FALSE(registry.cancel(second, operation));
  ASSERT_TRUE(registry.cancel(context, operation));
  port.finish(); pump();
  EXPECT_EQ(responses.at(1).result.error_code, ErrorCode::kCancelled);
  endpoint.disconnect(context);
  EXPECT_EQ(port.cleared, context.action.result_scope);
  endpoint.shutdown(); endpoint.shutdown();
  EXPECT_EQ(port.clears, 1);
  EXPECT_EQ(port.stops, 1);
  EXPECT_FALSE(endpoint.connectAuthenticated());
}
TEST_F(AutomationCoreTest, ShutdownCancelsPendingWorkBeforeReclaimingCallbacks) {
  submit(1, copy()); pump();
  ASSERT_EQ(port.calls, 1);
  endpoint.shutdown();
  EXPECT_EQ(responses.at(1).result.error_code, ErrorCode::kCancelled);
  EXPECT_EQ(scheduler.pending(), 0u);
  EXPECT_EQ(port.stops, 1);
  pump();
  EXPECT_EQ(port.calls, 1);
}
}  // namespace
}  // namespace qingying
