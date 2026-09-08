#include "qingying/app/action_handlers.hpp"
#include "qingying/app/capture_service.h"
#include "qingying/app/result_action_service.h"
#include "qingying/app/result_store.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <gtest/gtest.h>

#include <limits>

namespace qingying {
namespace {

class CropCenterHandlerTest : public ::testing::Test {
 protected:
  ActionDispatcher dispatcher;
  CaptureEngine capture;
  ExportService exporter;
  ResultStore results;
  PinManager pins;
  InteractionGate gate;
  ResultActionService actions{results, exporter, pins};
  ScreenPhysicalRect primary{-1921, -1081, 1921, 1081};
  ScreenPhysicalRect captured;
  int calls{0};
  CaptureService service{
      capture, results, pins, gate,
      [this](const ScreenPhysicalRect& region, Image& image) {
        captured = region;
        ++calls;
        image.width = region.width;
        image.height = region.height;
        image.pixels.assign(static_cast<std::size_t>(region.width) *
                                static_cast<std::size_t>(region.height),
                            0xFF010203u);
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      },
      [this] { return primary; }};

  void SetUp() override {
    registerAppHandlers(dispatcher, service, results, actions);
  }
};

TEST_F(CropCenterHandlerTest, UsesPrimaryPhysicalRectAndFloorsOddRemainder) {
  auto request = makeActionRequest(CropCenterRequest{100, 100});
  request.context.result_scope = 2;
  const auto result = dispatcher.dispatch(request);

  ASSERT_TRUE(result.ok);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(captured, (ScreenPhysicalRect{-1011, -591, 100, 100}));
  const auto* metadata = std::get_if<CapturedResult>(&result.output);
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->bounds, captured);
  EXPECT_TRUE(metadata->expires_at.has_value());
}

TEST_F(CropCenterHandlerTest, InvalidAndOversizeDimensionsPreserveOldResult) {
  const auto old = results.publish(2, Image{1, 1, {9}});
  ASSERT_NE(old, kInvalidResultId);
  for (const auto dimensions : {
           std::pair<int, int>{0, 1},
           std::pair<int, int>{1, -1},
           std::pair<int, int>{1922, 1},
           std::pair<int, int>{1, 1082}}) {
    auto request = makeActionRequest(
        CropCenterRequest{dimensions.first, dimensions.second});
    request.context.result_scope = 2;
    const auto result = dispatcher.dispatch(request);
    EXPECT_EQ(result.error_code, ErrorCode::kInvalidArgument);
    EXPECT_TRUE(results.acquire(2, old));
  }
  EXPECT_EQ(calls, 0);
}

TEST_F(CropCenterHandlerTest, RejectsCoordinateAndRowByteOverflowBeforeCapture) {
  auto request = makeActionRequest(CropCenterRequest{1, 1});
  request.context.result_scope = 2;
  primary = {(std::numeric_limits<int>::max)() - 1, 0, 2, 2};
  EXPECT_EQ(dispatcher.dispatch(request).error_code,
            ErrorCode::kInvalidArgument);

  primary = {0, 0, (std::numeric_limits<int>::max)(), 2};
  request.payload = CropCenterRequest{(std::numeric_limits<int>::max)(), 1};
  EXPECT_EQ(dispatcher.dispatch(request).error_code,
            ErrorCode::kInvalidArgument);
  EXPECT_EQ(calls, 0);
}

TEST(CropCenterHandlerBudgetTest, ResourceLimitPreservesPreviousResult) {
  AutomationLimits limits;
  limits.max_capture_pixels = 4;
  limits.max_result_bytes = 32;
  limits.max_retained_result_bytes = 48;
  ActionDispatcher dispatcher;
  CaptureEngine capture;
  ExportService exporter;
  ResultStore results(limits);
  PinManager pins;
  InteractionGate gate;
  ResultActionService actions(results, exporter, pins);
  int calls = 0;
  CaptureService service(capture, results, pins, gate,
      [&](const ScreenPhysicalRect&, Image&) {
        ++calls;
        return ActionResult{};
      },
      [] { return ScreenPhysicalRect{0, 0, 10, 10}; });
  registerAppHandlers(dispatcher, service, results, actions);
  const auto old = results.publish(2, Image{1, 1, {9}});
  ASSERT_NE(old, kInvalidResultId);
  auto request = makeActionRequest(CropCenterRequest{5, 1});
  request.context.result_scope = 2;

  EXPECT_EQ(dispatcher.dispatch(request).error_code,
            ErrorCode::kResourceLimit);
  EXPECT_TRUE(results.acquire(2, old));
  EXPECT_EQ(calls, 0);
}

TEST(CropCenterGeometryTest, ExactFitAndNegativeOddCentersAreDeterministic) {
  EXPECT_EQ(CaptureService::centeredRect({-9, -7, 7, 5}, 2, 2),
            (ScreenPhysicalRect{-7, -6, 2, 2}));
  EXPECT_EQ(CaptureService::centeredRect({-9, -7, 7, 5}, 7, 5),
            (ScreenPhysicalRect{-9, -7, 7, 5}));
  EXPECT_FALSE(CaptureService::centeredRect({0, 0, 3, 3}, 4, 1));
}

}  // namespace
}  // namespace qingying
