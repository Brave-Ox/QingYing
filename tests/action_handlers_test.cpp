#include "qingying/app/action_handlers.hpp"
#include "qingying/app/capture_service.h"
#include "qingying/app/result_action_service.h"
#include "qingying/app/result_store.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"
#include "qingying/automation/automation_contract.h"
#include "qingying/automation/action_catalog.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace {
class CopyCommitControl final : public qingying::IOperationControl {
 public:
  explicit CopyCommitControl(bool allow) : allow_(allow) {}
  bool requestCancel(qingying::AbortReason) override { return false; }
  bool tryCommit() override { ++commit_calls; return allow_; }
  qingying::OperationControlStatus status() override { return {}; }
  int commit_calls{0};
 private:
  bool allow_;
};
}  // namespace

TEST(AppActionHandlersTest, RegistersP0Handlers) {
  qingying::ActionDispatcher dispatcher;
  qingying::CaptureEngine capture;
  qingying::ExportService export_service;
  qingying::ResultStore result_store;
  qingying::PinManager pin_manager;
  qingying::InteractionGate gate;
  qingying::CaptureService capture_service(capture, result_store, pin_manager,
                                           gate);
  qingying::ResultActionService result_actions(result_store, export_service,
                                               pin_manager);

  qingying::registerAppHandlers(dispatcher, capture_service, result_store,
                                result_actions);

  for (const auto& entry : qingying::actionCatalog())
    if (entry.action) EXPECT_TRUE(dispatcher.hasHandler(*entry.action)) << entry.id;

  const qingying::ActionRequest status =
      qingying::makeActionRequest(qingying::StatusRequest{});
  const qingying::ActionResult status_result = dispatcher.dispatch(status);
  EXPECT_TRUE(status_result.ok);

  const qingying::ActionRequest copy =
      qingying::makeActionRequest(qingying::CopyRequest{});
  const qingying::ActionResult copy_result = dispatcher.dispatch(copy);
  EXPECT_FALSE(copy_result.ok);
  EXPECT_EQ(copy_result.error_code, qingying::ErrorCode::kNotReady);

  const qingying::ActionRequest region = qingying::makeActionRequest(
      qingying::CaptureRegionRequest{
          qingying::ScreenPhysicalRect{0, 0, 0, 0}});
  const qingying::ActionResult region_result = dispatcher.dispatch(region);
  EXPECT_FALSE(region_result.ok);
  EXPECT_EQ(region_result.error_code, qingying::ErrorCode::kInvalidArgument);

  const qingying::ActionRequest pin =
      qingying::makeActionRequest(qingying::PinRequest{});
  const qingying::ActionResult pin_result = dispatcher.dispatch(pin);
  EXPECT_FALSE(pin_result.ok);
  EXPECT_EQ(pin_result.error_code, qingying::ErrorCode::kNotReady);
}

TEST(AppActionHandlersTest, CopyHandlerUsesOperationCommitBeforeClipboardWrite) {
  using namespace qingying;
  ActionDispatcher dispatcher;
  CaptureEngine capture;
  ExportService exporter;
  ResultStore store;
  PinManager pins;
  InteractionGate gate;
  CaptureService capture_service(capture, store, pins, gate);
  int writes = 0;
  ResultActionService actions(store, exporter, pins, {}, nullptr, nullptr, {},
      [&](const Image&) {
        ++writes;
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      });
  registerAppHandlers(dispatcher, capture_service, store, actions);
  constexpr ResultScopeId scope = 9;
  const auto id = store.publish(scope, Image{1, 1, {0xFF010203u}});

  auto denied = std::make_shared<CopyCommitControl>(false);
  ActionRequest request{CopyRequest{ResultSelection::specific(id)}};
  request.context.result_scope = scope;
  request.operation_control = denied;
  EXPECT_EQ(dispatcher.dispatch(request).error_code, ErrorCode::kCancelled);
  EXPECT_EQ(denied->commit_calls, 1);
  EXPECT_EQ(writes, 0);

  auto allowed = std::make_shared<CopyCommitControl>(true);
  request.operation_control = allowed;
  const auto copied = dispatcher.dispatch(request);
  EXPECT_TRUE(copied.ok);
  EXPECT_EQ(allowed->commit_calls, 1);
  EXPECT_EQ(writes, 1);
  EXPECT_EQ(std::get<CopiedResult>(copied.output).result_id, id);
}

TEST(AppActionHandlersTest, PinHandlerUsesCommitAndReturnsRealPinId) {
  using namespace qingying;
  ActionDispatcher dispatcher;
  CaptureEngine capture;
  ExportService exporter;
  ResultStore store;
  int presentations = 0;
  PinManager pins({}, [&](PinWindow&, int, int) {
    ++presentations;
    return true;
  });
  InteractionGate gate;
  CaptureService capture_service(capture, store, pins, gate);
  ResultActionService actions(store, exporter, pins);
  registerAppHandlers(dispatcher, capture_service, store, actions);
  constexpr ResultScopeId scope = 10;
  const auto id = store.publish(scope, Image{1, 1, {0xFF010203u}});
  ActionRequest request{PinRequest{ResultSelection::specific(id)}};
  request.context.result_scope = scope;

  auto denied = std::make_shared<CopyCommitControl>(false);
  request.operation_control = denied;
  EXPECT_EQ(dispatcher.dispatch(request).error_code, ErrorCode::kCancelled);
  EXPECT_EQ(presentations, 0);
  EXPECT_EQ(pins.agentUsage().count, 0u);

  auto allowed = std::make_shared<CopyCommitControl>(true);
  request.operation_control = allowed;
  const auto pinned = dispatcher.dispatch(request);
  ASSERT_TRUE(pinned.ok);
  const auto metadata = std::get<PinnedResult>(pinned.output);
  EXPECT_EQ(metadata.result_id, id);
  EXPECT_NE(metadata.pin_id, kInvalidPinId);
  EXPECT_EQ(presentations, 1);
  EXPECT_EQ(pins.agentUsage().count, 1u);
}

TEST(AppActionHandlersTest, SuccessfulCapturePublishesExplicitResult) {
  qingying::ActionDispatcher dispatcher;
  qingying::CaptureEngine capture;
  qingying::ExportService export_service;
  qingying::ResultStore result_store;
  qingying::PinManager pin_manager;
  qingying::InteractionGate gate;
  qingying::ResultActionService result_actions(result_store, export_service,
                                               pin_manager);

  qingying::CaptureService capture_service(
      capture, result_store, pin_manager, gate,
      [](const qingying::ScreenPhysicalRect&, qingying::Image& out) {
        out = qingying::Image{2, 1, {0xFF112233u, 0xFF445566u}};
        qingying::ActionResult result;
        result.ok = true;
        result.error_code = qingying::ErrorCode::kOk;
        result.message = "injected capture success";
        return result;
      });
  qingying::registerAppHandlers(dispatcher, capture_service, result_store,
                                result_actions);

  const qingying::ActionRequest request = qingying::makeActionRequest(
      qingying::CaptureRegionRequest{
          qingying::ScreenPhysicalRect{0, 0, 2, 1}});
  const qingying::ActionResult result = dispatcher.dispatch(request);

  ASSERT_TRUE(result.ok);
  const auto current = result_store.current();
  ASSERT_TRUE(current.has_value());
  EXPECT_NE(current->result_id, qingying::kInvalidResultId);
  EXPECT_EQ(current->image.pixels,
            (std::vector<std::uint32_t>{0xFF112233u, 0xFF445566u}));
}

TEST(AppActionHandlersTest, PinUsesLatestCaptureResult) {
  qingying::ActionDispatcher dispatcher;
  qingying::CaptureEngine capture;
  qingying::ExportService export_service;
  qingying::ResultStore result_store;
  qingying::PinManager pin_manager;
  qingying::InteractionGate gate;
  qingying::CaptureService capture_service(capture, result_store, pin_manager,
                                           gate);
  qingying::ResultActionService result_actions(result_store, export_service,
                                               pin_manager);

  qingying::Image image;
  image.width = 2;
  image.height = 2;
  image.pixels.assign(4, 0xFF3366CCu);
  ASSERT_NE(result_store.publish(std::move(image)),
            qingying::kInvalidResultId);

  qingying::registerAppHandlers(dispatcher, capture_service, result_store,
                                result_actions);

  const qingying::ActionRequest pin =
      qingying::makeActionRequest(qingying::PinRequest{});
  const qingying::ActionResult result = dispatcher.dispatch(pin);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(pin_manager.count(), 1);
  pin_manager.closeAll();
}

TEST(AppActionHandlersTest, FailedCaptureReleasesPreviousResult) {
  qingying::ActionDispatcher dispatcher;
  qingying::CaptureEngine capture;
  qingying::ExportService export_service;
  qingying::ResultStore result_store;
  qingying::PinManager pin_manager;
  qingying::InteractionGate gate;
  qingying::ResultActionService result_actions(result_store, export_service,
                                               pin_manager);

  qingying::Image previous;
  previous.width = 2;
  previous.height = 1;
  previous.pixels = {0xFF112233u, 0xFF445566u};
  ASSERT_NE(result_store.publish(previous), qingying::kInvalidResultId);

  qingying::CaptureService capture_service(
      capture, result_store, pin_manager, gate,
      [](const qingying::ScreenPhysicalRect&, qingying::Image& out) {
        out = qingying::Image{};
        qingying::ActionResult result;
        result.ok = false;
        result.error_code = qingying::ErrorCode::kCaptureFailed;
        result.message = "injected capture failure";
        return result;
      });
  qingying::registerAppHandlers(dispatcher, capture_service, result_store,
                                result_actions);

  const qingying::ActionRequest request = qingying::makeActionRequest(
      qingying::CaptureRegionRequest{
          qingying::ScreenPhysicalRect{0, 0, 10, 10}});
  const qingying::ActionResult result = dispatcher.dispatch(request);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kCaptureFailed);
  EXPECT_EQ(result_store.currentId(), qingying::kInvalidResultId);
  EXPECT_FALSE(result_store.current().has_value());
}

TEST(AppActionHandlersTest, ScopedCaptureReportsMetadataAndFailureOnlyClearsItsScope) {
  using namespace qingying;
  ActionDispatcher dispatcher;
  CaptureEngine capture;
  ExportService exporter;
  PinManager pins;
  InteractionGate gate;
  ResultStore store;
  ResultActionService actions(store, exporter, pins);
  bool fail = false;
  CaptureService capture_service(capture, store, pins, gate,
      [&](const ScreenPhysicalRect&, Image& out) {
        ActionResult result;
        result.ok = !fail;
        result.error_code = fail ? ErrorCode::kCaptureFailed : ErrorCode::kOk;
        if (!fail) out = Image{2, 1, {21, 22}};
        return result;
      });
  registerAppHandlers(dispatcher, capture_service, store, actions);
  const auto gui = store.publish(Image{1, 1, {1}});
  const auto b = store.publish(3, Image{1, 1, {3}});
  auto request = makeActionRequest(CaptureRegionRequest{{-8, 9, 2, 1}});
  request.context.result_scope = 2;
  const auto result = dispatcher.dispatch(request);
  ASSERT_TRUE(result.ok);
  const auto* output = std::get_if<CapturedResult>(&result.output);
  ASSERT_NE(output, nullptr);
  EXPECT_EQ(output->result_id, store.currentId(2));
  EXPECT_EQ(output->width, 2);
  EXPECT_EQ(output->height, 1);
  EXPECT_EQ(output->bounds, (ScreenPhysicalRect{-8, 9, 2, 1}));
  for (ActionPayload payload : {ActionPayload{CopyRequest{ResultSelection::specific(b)}},
                               ActionPayload{SaveRequest{ResultSelection::specific(b), L"unused.png"}},
                               ActionPayload{PinRequest{ResultSelection::specific(b)}}}) {
    ActionRequest foreign{payload};
    foreign.context.result_scope = 2;
    EXPECT_FALSE(dispatcher.dispatch(foreign).ok);
  }
  fail = true;
  EXPECT_FALSE(dispatcher.dispatch(request).ok);
  EXPECT_EQ(store.currentId(2), kInvalidResultId);
  EXPECT_TRUE(store.acquire(3, b));
  EXPECT_TRUE(store.acquire(kGuiResultScopeId, gui));
}
