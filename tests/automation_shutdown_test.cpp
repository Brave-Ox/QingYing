#include "qingying/app/automation_runtime.h"
#include "qingying/app/automation_settings.h"
#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/capture_workflow.hpp"
#include "qingying/app/longshot_controller.hpp"
#include "qingying/app/result_action_service.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/ipc/pipe_automation_client.h"
#include "qingying/pin/pin_manager.hpp"
#include <gtest/gtest.h>
#include <future>

namespace qingying {
namespace {
class AutomationShutdownTest : public ::testing::Test {
 protected:
  ActionDispatcher dispatcher;
  CaptureEngine capture;
  LongShotEngine engine{capture};
  SelectionOverlay overlay;
  LongShotController controller{engine, overlay};
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  InteractionGate gate;
  ResultActionService actions{store, exporter, pins, {}, &gate};
  CaptureWorkflow workflow{dispatcher, capture, controller, store, actions, pins, overlay, &gate};
  OperationRegistry registry{901};
  std::vector<std::pair<UINT, UiMessageToken>> messages;
  TrustedAutomationContext last_context;
  UiActionScheduler scheduler{
    [this](UINT message, UiMessageToken token) { messages.emplace_back(message, token); return true; },
    [this](UiMessageToken token, const TrustedAutomationContext& context,
        const AutomationRequest& request, std::shared_ptr<OperationControl> control) {
      last_context = context;
      endpoint.execute(token, context, request, std::move(control));
    }};
  AutomationEndpoint endpoint{dispatcher, workflow, store, registry, scheduler, gate, {}, {}};
  ipc::PipeOptions options = [] {
    ipc::PipeOptions value;
    value.test_suffix = L"shutdown_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(GetTickCount64());
    return value;
  }();
  AutomationRuntime runtime{endpoint, scheduler, options};
  void pump() {
    runtime.tick();
    auto batch = std::move(messages); messages.clear();
    for (const auto& item : batch) scheduler.dispatch(item.first, item.second);
  }
  bool connect(ipc::PipeAutomationClient& client) {
    auto connected = std::async(std::launch::async, [&] { return client.connect(); });
    while (connected.wait_for(std::chrono::milliseconds(2)) != std::future_status::ready) pump();
    return connected.get();
  }
  AutomationResponse call(ipc::PipeAutomationClient& client, AutomationRequest request) {
    std::promise<AutomationResponse> completion;
    auto response = completion.get_future();
    client.submit(std::move(request), [&](AutomationResponse result) { completion.set_value(std::move(result)); });
    const auto deadline = GetTickCount64() + 5000;
    while (response.wait_for(std::chrono::milliseconds(2)) != std::future_status::ready && GetTickCount64() < deadline) pump();
    if (response.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
      client.close();
      ADD_FAILURE() << "pipe completion timed out";
    }
    return response.get();
  }
};
TEST_F(AutomationShutdownTest, DisableReenableChangesGenerationAndShutdownCollectsPendingReads) {
  ASSERT_TRUE(runtime.enable());
  ipc::PipeAutomationClient first(options);
  ASSERT_TRUE(connect(first));
  const auto old_connection = first.connection();
  AutomationRequest status; status.request_id = 10;
  ASSERT_TRUE(call(first, status).result.ok);
  const auto old_context = last_context;
  AutomationRequest original; original.request_id = 100;
  const auto operation = registry.begin(old_context, original); ASSERT_TRUE(operation.ok());
  const auto id = store.publish(old_context.action.result_scope, Image{1, 1, {1}});
  const auto handle = registry.bindResult(old_context, id); ASSERT_TRUE(handle);
  runtime.disable();
  EXPECT_FALSE(runtime.enabled());
  ASSERT_TRUE(runtime.enable());
  ipc::PipeAutomationClient second(options);
  ASSERT_TRUE(connect(second));
  EXPECT_EQ(second.connection().application_epoch, old_connection.application_epoch);
  EXPECT_NE(second.connection().generation, old_connection.generation);
  AutomationRequest stale_operation; stale_operation.request_id = 11;
  stale_operation.payload = GetOperationRequest{0, operation.handle};
  EXPECT_EQ(call(second, stale_operation).result.error_code, ErrorCode::kOperationNotFound);
  AutomationRequest stale_result; stale_result.request_id = 12;
  stale_result.payload = ReleaseResultRequest{0, *handle};
  EXPECT_EQ(call(second, stale_result).result.error_code, ErrorCode::kResultNotFound);
  EXPECT_FALSE(registry.get(old_context, operation.handle));
  // Restarting an endpoint uses a new epoch even if its local generation and
  // numeric identifiers start over.
  OperationRegistry restarted{902};
  const auto fresh_context = restarted.connect(old_context.action.result_scope);
  ASSERT_TRUE(fresh_context);
  EXPECT_FALSE(restarted.get(*fresh_context, operation.handle));
  EXPECT_FALSE(restarted.resolveResult(*fresh_context, *handle));
  AutomationRequest request; request.request_id = 1;
  std::promise<AutomationResponse> completion;
  auto response = completion.get_future();
  second.submit(request, [&](AutomationResponse result) { completion.set_value(std::move(result)); });
  // Shutdown without dispatching the request: admission is revoked and the
  // blocked client read must settle; repeating shutdown is harmless.
  runtime.shutdown(); runtime.shutdown(); runtime.disable();
  EXPECT_EQ(response.wait_for(std::chrono::seconds(3)), std::future_status::ready);
  EXPECT_FALSE(runtime.enable());
  EXPECT_FALSE(endpoint.connectAuthenticated());
}
TEST_F(AutomationShutdownTest, ShutdownInvalidatesScopesAndOpaqueHandlesBeforeDestruction) {
  auto context = endpoint.connectAuthenticated(); ASSERT_TRUE(context);
  AutomationRequest request; request.request_id = 1;
  const auto operation = registry.begin(*context, request); ASSERT_TRUE(operation.ok());
  const auto id = store.publish(context->action.result_scope, Image{1, 1, {1}});
  const auto handle = registry.bindResult(*context, id); ASSERT_TRUE(handle);
  runtime.shutdown();
  EXPECT_FALSE(registry.get(*context, operation.handle));
  EXPECT_FALSE(registry.resolveResult(*context, *handle));
  EXPECT_EQ(scheduler.queueUsage().queued, 0u);
}
TEST(AutomationSettingsTest, DefaultOffAndPersistsOnlyIsolatedKey) {
  const auto name = L"settings_" + std::to_wstring(GetCurrentProcessId());
  AutomationSettings settings(name);
  RegDeleteKeyW(HKEY_CURRENT_USER, settings.key().c_str());
  EXPECT_FALSE(settings.enabled());
  EXPECT_TRUE(settings.setEnabled(true));
  EXPECT_TRUE(AutomationSettings(name).enabled());
  EXPECT_TRUE(settings.setEnabled(false));
  EXPECT_FALSE(AutomationSettings(name).enabled());
  EXPECT_EQ(RegDeleteKeyW(HKEY_CURRENT_USER, settings.key().c_str()), ERROR_SUCCESS);
}
}  // namespace
}  // namespace qingying
