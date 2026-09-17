#include "qingying/automation/automation_endpoint.h"
#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/automation_workflow_adapter.h"
#include "qingying/app/capture_workflow.hpp"
#include "qingying/app/capture_service.h"
#include "qingying/app/longshot_controller.hpp"
#include "qingying/app/result_action_service.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"
#include <gtest/gtest.h>

namespace qingying {
namespace {
class DeferredCopy final : public IAsyncActionHandler {
 public:
  ActionType type() const override { return ActionType::Copy; }
  void handleAsync(const ActionRequest& value, ActionCompletion callback,
                   const ActionExecutor&) override {
    request = value;
    completion = std::move(callback);
    ++calls;
  }
  void finish(bool commit) {
    ActionResult result;
    result.ok = commit && request.operation_control->tryCommit();
    result.error_code = result.ok ? ErrorCode::kOk : ErrorCode::kCancelled;
    completion(std::move(result));
  }
  ActionRequest request;
  ActionCompletion completion;
  int calls{0};
};
class SyncCapture final : public IActionHandler {
 public:
  explicit SyncCapture(ResultStore& store) : store_(store) {}
  ActionType type() const override { return ActionType::CaptureRegion; }
  ActionResult handle(const ActionRequest& request) override {
    ActionResult result;
    result.ok = request.operation_control->tryCommit();
    result.error_code = result.ok ? ErrorCode::kOk : ErrorCode::kCancelled;
    if (result.ok) {
      const auto id = store_.publish(request.context.result_scope, Image{1, 1, {1}});
      result.output = CapturedResult{id, 1, 1};
    }
    return result;
  }
 private:
  ResultStore& store_;
};
class AutomationEndpointTest : public ::testing::Test {
 protected:
  std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
  ActionDispatcher dispatcher;
  CaptureEngine capture;
  LongShotEngine engine{capture};
  SelectionOverlay overlay;
  LongShotController controller{engine, overlay};
  ResultStore store{{}, [this] { return now; }};
  ExportService exporter;
  PinManager pins;
  InteractionGate gate;
  ResultActionService actions{store, exporter, pins, {}, &gate};
  CaptureService capture_service{capture, store, pins, gate};
  CaptureWorkflow workflow{capture, capture_service, controller, store,
                           actions, pins, overlay, &gate};
  AutomationWorkflowAdapter adapter{dispatcher, workflow, store, {
      {ActionType::Copy, "copy", false},
      {ActionType::CaptureRegion, "capture_region", false}}};
  OperationRegistry registry{123, {}, [this] { return now; }};
  std::vector<std::pair<UINT, UiMessageToken>> messages;
  bool post_ok{true};
  UiActionScheduler scheduler{
      [this](UINT message, UiMessageToken ticket) {
        if (post_ok) messages.emplace_back(message, ticket);
        return post_ok;
      },
      [this](UiMessageToken ticket, const TrustedAutomationContext& context,
             const AutomationRequest& request, std::shared_ptr<OperationControl> control) {
        endpoint->execute(ticket, context, request, std::move(control));
      }, {}, [this] { return now; }};
  std::unique_ptr<AutomationEndpoint> endpoint;
  TrustedAutomationContext first, second;
  std::map<RequestId, AutomationResponse> responses;
  DeferredCopy* deferred{nullptr};
  RequestId next_request{1};
  void initialize(bool ready = false) {
    AutomationEndpoint::ExecutionPolicy policy;
    policy.agent_pin_usage = [] { return std::make_pair(3u, 12ULL); };
    if (ready) {
      policy.ready_actions = {ActionType::Copy, ActionType::CaptureRegion};
      policy.stop_producers = [this] {
        if (deferred && deferred->completion) deferred->finish(false);
      };
    }
    endpoint = std::make_unique<AutomationEndpoint>(adapter, adapter,
        registry, scheduler, gate, AutomationLimits{}, std::move(policy));
    first = *endpoint->connectAuthenticated();
    second = *endpoint->connectAuthenticated();
  }
  void SetUp() override { initialize(); }
  void TearDown() override { endpoint->shutdown(); endpoint.reset(); }
  void enableHandlers() {
    // Only fake handlers that honor the control/cleanup contract are enabled.
    auto handler = std::make_unique<DeferredCopy>();
    deferred = handler.get();
    dispatcher.registerAsyncHandler(std::move(handler));
    dispatcher.registerHandler(std::make_unique<SyncCapture>(store));
  }
  RequestId submit(const TrustedAutomationContext& context, AutomationPayload payload) {
    AutomationRequest request;
    request.request_id = next_request++;
    request.payload = std::move(payload);
    auto admitted = context;
    admitted.submitted_at = now;
    scheduler.submit(admitted, request, [this](AutomationResponse response) {
      responses.emplace(response.result.request_id, std::move(response));
    });
    return request.request_id;
  }
  void pump() {
    while (!messages.empty()) {
      auto batch = std::move(messages);
      messages.clear();
      for (auto message : batch) scheduler.dispatch(message.first, message.second);
    }
  }
  ExecuteActionRequest copyRequest(std::optional<std::string> key = {}) {
    return {CopyRequest{ResultSelection::specific(99)}, key};
  }
};
class ReadyEndpointTest : public AutomationEndpointTest {
  void SetUp() override { initialize(true); enableHandlers(); }
};
TEST_F(AutomationEndpointTest, TruthfulStatusIncludesOccupancyQueuesBudgetAndSafeCapabilities) {
  const auto result = store.publish(first.action.result_scope, Image{1, 1, {1}});
  auto reservation = store.reserve(first.action.result_scope, 2, 2);
  ASSERT_NE(result, kInvalidResultId);
  auto gui = gate.acquire(InteractionKind::Capture);
  const auto id = submit(first, ExecuteActionRequest{});
  EXPECT_EQ(scheduler.queueUsage().queued, 1u);
  pump();
  ASSERT_TRUE(responses.at(id).result.ok);
  const auto& info = std::get<StatusInfo>(responses.at(id).result.output);
  EXPECT_EQ(info.busy, true);
  EXPECT_EQ(info.automation_enabled, false);
  EXPECT_EQ(info.connection_reason, "transport_not_enabled");
  EXPECT_EQ(info.busy_reason, "capture");
  ASSERT_TRUE(info.resources);
  EXPECT_EQ(info.resources->result_bytes, 4u);
  EXPECT_EQ(info.resources->reserved_result_bytes, 16u);
  EXPECT_EQ(info.resources->agent_pin_count, 3u);
  EXPECT_EQ(info.resources->agent_pin_bytes, 12u);
  ASSERT_TRUE(info.queues);
  EXPECT_EQ(info.queues->running, 1u);
  EXPECT_EQ(info.capabilities, (std::vector<std::string>{"status", "get_operation", "cancel_operation", "release_result"}));
}
TEST_F(AutomationEndpointTest, OpaqueOperationHandlesAreResolvedInsideOwnerScope) {
  AutomationRequest original;
  original.request_id = 9000;
  const auto created = registry.begin(first, original);
  ASSERT_TRUE(created.ok());
  const auto good = submit(first, GetOperationRequest{0, created.handle});
  const auto foreign = submit(second, GetOperationRequest{0, created.handle});
  pump();
  EXPECT_TRUE(responses.at(good).result.ok);
  EXPECT_EQ(responses.at(good).result.operation_id, created.operation_id);
  EXPECT_EQ(responses.at(foreign).result.error_code, ErrorCode::kOperationNotFound);
  const auto cancel_foreign = submit(second, CancelOperationRequest{OperationCancellation{}, created.handle});
  const auto cancel_owner = submit(first, CancelOperationRequest{OperationCancellation{}, created.handle});
  pump();
  EXPECT_EQ(responses.at(cancel_foreign).result.error_code, ErrorCode::kOperationNotFound);
  EXPECT_TRUE(responses.at(cancel_owner).result.ok);
}
TEST_F(AutomationEndpointTest, OpaqueReleaseRejectsOtherScopeAndAllowsIdempotentOwnerRelease) {
  const auto id = store.publish(first.action.result_scope, Image{1, 1, {1}});
  const auto handle = registry.bindResult(first, id);
  ASSERT_TRUE(handle);
  const auto foreign = submit(second, ReleaseResultRequest{0, handle});
  pump();
  EXPECT_EQ(responses.at(foreign).result.error_code, ErrorCode::kResultNotFound);
  EXPECT_EQ(store.resultStatus(first.action.result_scope, id), ErrorCode::kOk);
  const auto owner = submit(first, ReleaseResultRequest{0, handle}); pump();
  EXPECT_TRUE(responses.at(owner).result.ok);
  const auto repeat = submit(first, ReleaseResultRequest{0, handle}); pump();
  ASSERT_TRUE(responses.at(repeat).result.ok);
  EXPECT_TRUE(std::get<ReleasedResult>(responses.at(repeat).control).already_released);
}
TEST_F(AutomationEndpointTest, UnsafeCaptureIsUnadvertisedAndBusyHasPrecedence) {
  const ExecuteActionRequest capture_request{CaptureRegionRequest{{0, 0, 1, 1}}, {}};
  const auto unsupported = submit(first, capture_request);
  pump();
  EXPECT_EQ(responses.at(unsupported).result.error_code, ErrorCode::kNotImplemented);
  auto gui = gate.acquire(InteractionKind::Capture);
  const auto blocked = submit(first, capture_request);
  const auto longshot = submit(second, BeginLongShotRequest{});
  pump();
  EXPECT_EQ(responses.at(blocked).result.error_code, ErrorCode::kBusy);
  EXPECT_EQ(responses.at(longshot).result.error_code, ErrorCode::kBusy);
}
TEST_F(AutomationEndpointTest, ReleaseAndDisconnectOnlyClearTheirOwnScope) {
  const auto gui = store.publish(Image{1, 1, {1}});
  const auto a = store.publish(first.action.result_scope, Image{1, 1, {2}});
  const auto b = store.publish(second.action.result_scope, Image{1, 1, {3}});
  const auto foreign = submit(second, ReleaseResultRequest{a});
  pump();
  EXPECT_EQ(responses.at(foreign).result.error_code, ErrorCode::kResultNotFound);
  EXPECT_TRUE(store.acquire(first.action.result_scope, a));
  const auto release = submit(first, ReleaseResultRequest{a});
  pump();
  EXPECT_TRUE(responses.at(release).result.ok);
  const auto repeat = submit(first, ReleaseResultRequest{a});
  pump();
  EXPECT_TRUE(std::get<ReleasedResult>(responses.at(repeat).control).already_released);
  endpoint->disconnect(first);
  EXPECT_TRUE(store.acquire(kGuiResultScopeId, gui));
  EXPECT_TRUE(store.acquire(second.action.result_scope, b));
  const auto rejected = submit(first, ExecuteActionRequest{});
  EXPECT_EQ(responses.at(rejected).result.error_code, ErrorCode::kCancelled);
  const auto reconnected = endpoint->connectAuthenticated();
  ASSERT_TRUE(reconnected);
  EXPECT_NE(reconnected->action.result_scope, first.action.result_scope);
  EXPECT_GT(reconnected->connection.generation, first.connection.generation);
}
TEST_F(AutomationEndpointTest, ConnectionLimitAndForeignContextAreRejected) {
  ASSERT_TRUE(endpoint->connectAuthenticated());
  ASSERT_TRUE(endpoint->connectAuthenticated());
  EXPECT_FALSE(endpoint->connectAuthenticated());
  auto forged = first;
  forged.action.result_scope = second.action.result_scope;
  const auto id = submit(forged, ExecuteActionRequest{});
  EXPECT_EQ(responses.at(id).result.error_code, ErrorCode::kCancelled);
}
TEST_F(AutomationEndpointTest, PinModalSaveBlocksExternalCaptureWhileStatusStillWorks) {
  ResultActionService modal(store, exporter, pins, [&](HWND) {
    EXPECT_FALSE(workflow.active());
    const auto status_id = submit(first, ExecuteActionRequest{});
    const auto capture_id = submit(second, BeginLongShotRequest{});
    pump();
    EXPECT_EQ(std::get<StatusInfo>(responses.at(status_id).result.output).busy, true);
    EXPECT_EQ(responses.at(capture_id).result.error_code, ErrorCode::kBusy);
    return std::optional<std::wstring>{};
  }, &gate);
  EXPECT_TRUE(modal.savePinImage(Image{1, 1, {1}}).ok);
  EXPECT_FALSE(gate.busy());
}
TEST_F(ReadyEndpointTest, SynchronousCapturePublishesScopedMetadataAndTtlInvalidatesHandle) {
  const auto id = submit(first, ExecuteActionRequest{CaptureRegionRequest{{0, 0, 1, 1}}, {}});
  pump();
  const auto& response = responses.at(id);
  ASSERT_TRUE(response.result.ok);
  const auto result = std::get<CapturedResult>(response.result.output).result_id;
  EXPECT_TRUE(store.acquire(first.action.result_scope, result));
  EXPECT_FALSE(store.acquire(second.action.result_scope, result));
  const auto handle = registry.bindResult(first, result);
  ASSERT_TRUE(handle);
  now += std::chrono::seconds{61};
  endpoint->tick();
  const auto snapshot = registry.get(first, response.result.operation_id);
  ASSERT_TRUE(snapshot);
  EXPECT_EQ(snapshot->state, OperationState::Succeeded);
  EXPECT_EQ(snapshot->result_availability, ResultAvailability::Expired);
  EXPECT_FALSE(registry.resolveResult(first, *handle));
  EXPECT_FALSE(gate.busy());
}
TEST_F(ReadyEndpointTest, AsyncCancellationKeepsBusyUntilWorkerAndCompletionDelivery) {
  const auto id = submit(first, copyRequest());
  pump();
  ASSERT_EQ(deferred->calls, 1);
  EXPECT_TRUE(gate.busy());
  const auto op = deferred->request.operation_id;
  const auto cancel = submit(first, CancelOperationRequest{OperationCancellation{op}});
  const auto foreign = submit(second, GetOperationRequest{op});
  pump();
  EXPECT_EQ(std::get<CancellationResult>(responses.at(cancel).control).state, OperationState::Cancelling);
  EXPECT_EQ(responses.at(foreign).result.error_code, ErrorCode::kOperationNotFound);
  EXPECT_TRUE(gate.busy());
  EXPECT_EQ(responses.count(id), 0u);
  post_ok = false;
  std::thread worker([&] { deferred->finish(false); });
  worker.join();
  EXPECT_TRUE(gate.busy());
  endpoint->tick();
  EXPECT_FALSE(gate.busy());
  EXPECT_EQ(responses.at(id).result.error_code, ErrorCode::kCancelled);
  EXPECT_EQ(registry.get(first, op)->state, OperationState::Cancelled);
}
TEST_F(ReadyEndpointTest, RetrySharesExecutionAndCompletionRetainsGuardAcrossReentry) {
  const auto a = submit(first, copyRequest("same-key"));
  const auto b = submit(first, copyRequest("same-key"));
  pump();
  ASSERT_EQ(deferred->calls, 1);
  deferred->finish(true);
  pump();
  ASSERT_TRUE(responses.at(a).result.ok);
  ASSERT_TRUE(responses.at(b).result.ok);
  EXPECT_EQ(responses.at(a).result.operation_id, responses.at(b).result.operation_id);
  EXPECT_EQ(responses.at(b).result.request_id, b);
  EXPECT_FALSE(gate.busy());
  const auto c = submit(first, copyRequest("same-key"));
  pump();
  EXPECT_TRUE(responses.at(c).result.ok);
  EXPECT_EQ(deferred->calls, 1);
}
TEST_F(ReadyEndpointTest, CompletionCallbackCannotReenterBeforeGuardRelease) {
  AutomationRequest request;
  request.request_id = 99;
  request.payload = copyRequest();
  auto admitted = first;
  admitted.submitted_at = now;
  bool completed = false;
  scheduler.submit(admitted, request, [&](AutomationResponse) {
    completed = true;
    EXPECT_TRUE(gate.busy());
    const auto nested = submit(second, BeginLongShotRequest{});
    pump();
    EXPECT_EQ(responses.at(nested).result.error_code, ErrorCode::kBusy);
  });
  pump();
  deferred->finish(true);
  pump();
  EXPECT_TRUE(completed);
  EXPECT_FALSE(gate.busy());
}
TEST_F(ReadyEndpointTest, DisconnectRevokesGenerationButWaitsForActualCleanup) {
  const auto id = submit(first, copyRequest());
  pump();
  endpoint->disconnect(first);
  EXPECT_TRUE(gate.busy());
  EXPECT_EQ(deferred->request.operation_control->status().abort_reason, AbortReason::Disconnect);
  deferred->finish(false);
  pump();
  EXPECT_EQ(responses.at(id).result.error_code, ErrorCode::kCancelled);
  EXPECT_FALSE(gate.busy());
  const auto status_id = submit(second, ExecuteActionRequest{});
  pump();
  EXPECT_TRUE(responses.at(status_id).result.ok);
}
TEST_F(ReadyEndpointTest, ShutdownJoinsProducersThenSettlesAndIsIdempotent) {
  const auto id = submit(first, copyRequest());
  pump();
  endpoint->shutdown();
  endpoint->shutdown();
  EXPECT_EQ(responses.at(id).result.error_code, ErrorCode::kCancelled);
  EXPECT_EQ(scheduler.pending(), 0u);
  EXPECT_FALSE(gate.busy());
  EXPECT_FALSE(endpoint->connectAuthenticated());
  EXPECT_EQ(endpoint->status().app_running, false);
  EXPECT_FALSE(workflow.beginSelection());
  pump(); // stale tokens do not execute again
  EXPECT_EQ(deferred->calls, 1);
}
TEST_F(AutomationEndpointTest, EndpointEnforcesUiThreadOwnership) {
  std::thread worker([&] {
    EXPECT_THROW(endpoint->status(), std::logic_error);
    EXPECT_THROW(endpoint->connectAuthenticated(), std::logic_error);
    EXPECT_THROW(endpoint->tick(), std::logic_error);
  });
  worker.join();
}
TEST_F(ReadyEndpointTest, PrivateCancellationAcknowledgesQueuedRequestBeforeOperationAllocation) {
  const auto id = submit(first, copyRequest());
  const auto cancel = submit(first, CancelOperationRequest{RequestCancellation{id}});
  pump();
  EXPECT_EQ(deferred->calls, 0);
  EXPECT_EQ(responses.at(id).result.error_code, ErrorCode::kCancelled);
  ASSERT_TRUE(responses.at(cancel).result.ok);
  const auto receipt = std::get<CancellationResult>(responses.at(cancel).control);
  EXPECT_TRUE(receipt.cancellation_requested);
  EXPECT_EQ(receipt.operation_id, kInvalidOperationId);
  EXPECT_FALSE(gate.busy());
}
TEST_F(ReadyEndpointTest, PrivateCancellationCannotTargetAnotherConnection) {
  const auto id = submit(first, copyRequest());
  const auto cancel = submit(second, CancelOperationRequest{RequestCancellation{id}});
  pump();
  EXPECT_EQ(responses.at(cancel).result.error_code, ErrorCode::kOperationNotFound);
  EXPECT_EQ(deferred->request.operation_control->status().abort_reason, AbortReason::None);
  deferred->finish(true);
  pump();
  EXPECT_TRUE(responses.at(id).result.ok);
}
TEST_F(ReadyEndpointTest, PrivateCancellationOfAdmittedRetryCancelsSharedOperation) {
  const auto original = submit(first, copyRequest("same"));
  const auto retry = submit(first, copyRequest("same"));
  pump();
  const auto cancel = submit(first, CancelOperationRequest{RequestCancellation{retry}});
  pump();
  ASSERT_TRUE(responses.at(cancel).result.ok);
  const auto receipt = std::get<CancellationResult>(responses.at(cancel).control);
  EXPECT_EQ(receipt.operation_id, deferred->request.operation_id);
  EXPECT_EQ(receipt.state, OperationState::Cancelling);
  EXPECT_TRUE(gate.busy());
  deferred->finish(false);
  pump();
  EXPECT_EQ(responses.at(original).result.error_code, ErrorCode::kCancelled);
  EXPECT_EQ(responses.at(retry).result.error_code, ErrorCode::kCancelled);
  EXPECT_FALSE(gate.busy());
}
TEST_F(ReadyEndpointTest, FailedOperationIsASuccessfulScopedQuery) {
  const auto id = submit(first, copyRequest());
  pump();
  const auto op = deferred->request.operation_id;
  ActionResult failure;
  failure.error_code = ErrorCode::kExportFailed;
  deferred->completion(failure);
  pump();
  EXPECT_FALSE(responses.at(id).result.ok);
  const auto query = submit(first, GetOperationRequest{op});
  pump();
  ASSERT_TRUE(responses.at(query).result.ok);
  const auto snapshot = std::get<OperationSnapshot>(responses.at(query).control);
  EXPECT_EQ(snapshot.state, OperationState::Failed);
  ASSERT_TRUE(snapshot.outcome);
  EXPECT_EQ(snapshot.outcome->error_code, ErrorCode::kExportFailed);
}
TEST_F(ReadyEndpointTest, ShutdownDuringCompletionCallbackRetainsOccupancyUntilReturn) {
  AutomationRequest request;
  request.request_id = 99;
  request.payload = copyRequest();
  bool completed = false;
  scheduler.submit(first, request, [&](AutomationResponse) {
    endpoint->shutdown();
    EXPECT_TRUE(gate.busy());
    EXPECT_FALSE(endpoint->connectAuthenticated());
    completed = true;
  });
  pump();
  deferred->finish(true);
  pump();
  EXPECT_TRUE(completed);
  EXPECT_FALSE(gate.busy());
}
TEST_F(AutomationEndpointTest, GuiAdmissionHonorsSharedGateBeforeClearingCurrentResult) {
  const HWND window = CreateWindowExW(0, L"STATIC", L"F9 test", 0,
      0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
  ASSERT_NE(window, nullptr);
  workflow.setOwnerWindow(window);
  const auto result = store.publish(Image{1, 1, {1}});
  auto dialog = gate.acquire(InteractionKind::SaveDialog);
  EXPECT_FALSE(workflow.beginSelection());
  EXPECT_TRUE(store.acquire(kGuiResultScopeId, result));
  EXPECT_TRUE(gate.busy());
  workflow.setOwnerWindow(nullptr);
  DestroyWindow(window);
}
}  // namespace
}  // namespace qingying
