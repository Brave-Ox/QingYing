#include "qingying/app/action_handlers.hpp"
#include "qingying/app/capture_session.hpp"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <gtest/gtest.h>

#include <utility>

TEST(AppActionHandlersTest, RegistersP0Handlers) {
  qingying::ActionDispatcher dispatcher;
  qingying::CaptureEngine capture;
  qingying::ExportService export_service;
  qingying::CaptureSession session;
  qingying::PinManager pin_manager;

  qingying::registerAppHandlers(dispatcher, capture, export_service, session,
                                pin_manager);

  qingying::ActionRequest status;
  status.type = qingying::ActionType::Status;
  const qingying::ActionResult status_result = dispatcher.dispatch(status);
  EXPECT_TRUE(status_result.ok);

  qingying::ActionRequest copy;
  copy.type = qingying::ActionType::Copy;
  const qingying::ActionResult copy_result = dispatcher.dispatch(copy);
  EXPECT_FALSE(copy_result.ok);
  EXPECT_EQ(copy_result.error_code, qingying::ErrorCode::kNotReady);

  qingying::ActionRequest region;
  region.type = qingying::ActionType::CaptureRegion;
  region.width = 0;  // 非法宽高：handler 应在触达截屏前就拒绝，避免 GDI 环境依赖
  region.height = 0;
  const qingying::ActionResult region_result = dispatcher.dispatch(region);
  EXPECT_FALSE(region_result.ok);
  EXPECT_EQ(region_result.error_code, qingying::ErrorCode::kInvalidArgument);

  qingying::ActionRequest pin;
  pin.type = qingying::ActionType::Pin;
  const qingying::ActionResult pin_result = dispatcher.dispatch(pin);
  EXPECT_FALSE(pin_result.ok);
  EXPECT_EQ(pin_result.error_code, qingying::ErrorCode::kNotReady);
}

TEST(AppActionHandlersTest, PinUsesLatestCaptureResult) {
  qingying::ActionDispatcher dispatcher;
  qingying::CaptureEngine capture;
  qingying::ExportService export_service;
  qingying::CaptureSession session;
  qingying::PinManager pin_manager;

  qingying::Image image;
  image.width = 2;
  image.height = 2;
  image.pixels.assign(4, 0xFF3366CCu);
  session.setResult(std::move(image));

  qingying::registerAppHandlers(dispatcher, capture, export_service, session,
                                pin_manager);

  qingying::ActionRequest pin;
  pin.type = qingying::ActionType::Pin;
  const qingying::ActionResult result = dispatcher.dispatch(pin);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(pin_manager.count(), 1);
  pin_manager.closeAll();
}
