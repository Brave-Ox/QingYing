#include "qingying/app/action_handlers.hpp"
#include "qingying/app/result_action_service.h"
#include "qingying/app/result_store.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

TEST(AppActionHandlersTest, RegistersP0Handlers) {
  qingying::ActionDispatcher dispatcher;
  qingying::CaptureEngine capture;
  qingying::ExportService export_service;
  qingying::ResultStore result_store;
  qingying::PinManager pin_manager;
  qingying::ResultActionService result_actions(result_store, export_service,
                                               pin_manager);

  qingying::registerAppHandlers(dispatcher, capture, result_store,
                                result_actions);

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

TEST(AppActionHandlersTest, SuccessfulCapturePublishesExplicitResult) {
  qingying::ActionDispatcher dispatcher;
  qingying::CaptureEngine capture;
  qingying::ExportService export_service;
  qingying::ResultStore result_store;
  qingying::PinManager pin_manager;
  qingying::ResultActionService result_actions(result_store, export_service,
                                               pin_manager);

  const qingying::CaptureRegionInvoker capture_region =
      [](const qingying::ActionRequest&, qingying::Image& out) {
        out = qingying::Image{2, 1, {0xFF112233u, 0xFF445566u}};
        qingying::ActionResult result;
        result.ok = true;
        result.error_code = qingying::ErrorCode::kOk;
        result.message = "injected capture success";
        return result;
      };
  qingying::registerAppHandlers(dispatcher, capture, result_store,
                                result_actions, capture_region);

  const qingying::ActionRequest request = qingying::makeActionRequest(
      qingying::CaptureRegionRequest{
          qingying::ScreenPhysicalRect{0, 0, 10, 10}});
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
  qingying::ResultActionService result_actions(result_store, export_service,
                                               pin_manager);

  qingying::Image image;
  image.width = 2;
  image.height = 2;
  image.pixels.assign(4, 0xFF3366CCu);
  ASSERT_NE(result_store.publish(std::move(image)),
            qingying::kInvalidResultId);

  qingying::registerAppHandlers(dispatcher, capture, result_store,
                                result_actions);

  const qingying::ActionRequest pin =
      qingying::makeActionRequest(qingying::PinRequest{});
  const qingying::ActionResult result = dispatcher.dispatch(pin);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(pin_manager.count(), 1);
  pin_manager.closeAll();
}

TEST(AppActionHandlersTest, FailedCaptureKeepsPreviousResult) {
  qingying::ActionDispatcher dispatcher;
  qingying::CaptureEngine capture;
  qingying::ExportService export_service;
  qingying::ResultStore result_store;
  qingying::PinManager pin_manager;
  qingying::ResultActionService result_actions(result_store, export_service,
                                               pin_manager);

  qingying::Image previous;
  previous.width = 2;
  previous.height = 1;
  previous.pixels = {0xFF112233u, 0xFF445566u};
  ASSERT_NE(result_store.publish(previous), qingying::kInvalidResultId);

  const qingying::CaptureRegionInvoker fail_capture =
      [](const qingying::ActionRequest&, qingying::Image& out) {
        out = qingying::Image{};
        qingying::ActionResult result;
        result.ok = false;
        result.error_code = qingying::ErrorCode::kCaptureFailed;
        result.message = "injected capture failure";
        return result;
      };
  qingying::registerAppHandlers(dispatcher, capture, result_store,
                                result_actions, fail_capture);

  const qingying::ActionRequest request = qingying::makeActionRequest(
      qingying::CaptureRegionRequest{
          qingying::ScreenPhysicalRect{0, 0, 10, 10}});
  const qingying::ActionResult result = dispatcher.dispatch(request);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kCaptureFailed);
  const auto current = result_store.current();
  ASSERT_TRUE(current.has_value());
  EXPECT_EQ(current->image.width, previous.width);
  EXPECT_EQ(current->image.height, previous.height);
  EXPECT_EQ(current->image.pixels, previous.pixels);
}
