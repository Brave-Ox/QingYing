#include "qingying/app/action_handlers.hpp"
#include "qingying/app/capture_session.hpp"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"

#include <gtest/gtest.h>

TEST(AppActionHandlersTest, RegistersP0Handlers) {
  qingying::ActionDispatcher dispatcher;
  qingying::CaptureEngine capture;
  qingying::ExportService export_service;
  qingying::CaptureSession session;

  qingying::registerAppHandlers(dispatcher, capture, export_service, session);

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
  region.width = 100;
  region.height = 100;
  const qingying::ActionResult region_result = dispatcher.dispatch(region);
  EXPECT_FALSE(region_result.ok);
  EXPECT_EQ(region_result.error_code, qingying::ErrorCode::kNotImplemented);
}
