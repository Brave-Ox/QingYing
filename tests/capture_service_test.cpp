#include "qingying/app/capture_service.h"

#include "qingying/app/result_store.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <gtest/gtest.h>

namespace qingying {
namespace {

AutomationLimits smallCaptureBudget() {
  AutomationLimits limits;
  limits.max_capture_pixels = 4;
  limits.max_result_bytes = 32;
  limits.max_retained_result_bytes = 48;
  return limits;
}

ActionRequest requestFor(ResultScopeId scope) {
  auto request = makeActionRequest(CaptureRegionRequest{{0, 0, 1, 1}});
  request.context.result_scope = scope;
  return request;
}

TEST(CaptureServiceTest, BusyAndBudgetPreflightPreservePreviousResult) {
  CaptureEngine capture;
  ResultStore results(smallCaptureBudget());
  PinManager pins;
  InteractionGate gate;
  int calls = 0;
  CaptureService service(capture, results, pins, gate,
      [&](const ScreenPhysicalRect&, Image&) {
        ++calls;
        return ActionResult{};
      });
  const auto old = results.publish(2, Image{1, 1, {7}});
  ASSERT_NE(old, kInvalidResultId);

  auto active = gate.acquire(InteractionKind::SaveDialog);
  ASSERT_TRUE(active);
  const auto busy = service.capture(requestFor(2), {0, 0, 1, 1});
  EXPECT_EQ(busy.error_code, ErrorCode::kBusy);
  EXPECT_TRUE(results.acquire(2, old));
  active.reset();

  const auto limited = service.capture(requestFor(2), {0, 0, 5, 1});
  EXPECT_EQ(limited.error_code, ErrorCode::kResourceLimit);
  EXPECT_TRUE(results.acquire(2, old));
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(results.budgetSnapshot().reserved_bytes, 0u);
}

TEST(CaptureServiceTest, AcceptedFailureClearsOnlyItsScopeAndRestoresPins) {
  CaptureEngine capture;
  ResultStore results;
  PinManager pins;
  InteractionGate gate;
  bool excluded_during_capture = false;
  CaptureService service(capture, results, pins, gate,
      [&](const ScreenPhysicalRect&, Image&) {
        excluded_during_capture = pins.captureExclusionActive();
        ActionResult result;
        result.error_code = ErrorCode::kCaptureFailed;
        result.message = "injected failure";
        return result;
      });
  const auto old = results.publish(2, Image{1, 1, {2}});
  const auto other = results.publish(3, Image{1, 1, {3}});
  ASSERT_NE(old, kInvalidResultId);
  ASSERT_NE(other, kInvalidResultId);

  const auto result = service.capture(requestFor(2), {10, 20, 1, 1});
  EXPECT_EQ(result.error_code, ErrorCode::kCaptureFailed);
  EXPECT_TRUE(excluded_during_capture);
  EXPECT_FALSE(pins.captureExclusionActive());
  EXPECT_FALSE(results.acquire(2, old));
  EXPECT_TRUE(results.acquire(3, other));
  EXPECT_EQ(results.budgetSnapshot().reserved_bytes, 0u);
}

TEST(CaptureServiceTest, PublishesBoundsModeAndExternalExpiryFromOneCapture) {
  auto now = std::chrono::steady_clock::now();
  CaptureEngine capture;
  ResultStore results({}, [&] { return now; });
  PinManager pins;
  InteractionGate gate;
  int calls = 0;
  CaptureService service(capture, results, pins, gate,
      [&](const ScreenPhysicalRect& region, Image& image) {
        ++calls;
        image = Image{region.width, region.height, {11, 12}};
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      });
  const ScreenPhysicalRect region{-8, 9, 2, 1};
  const auto result = service.capture(requestFor(2), region);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(calls, 1);
  const auto* metadata = std::get_if<CapturedResult>(&result.output);
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->bounds, region);
  EXPECT_EQ(metadata->width, 2);
  EXPECT_EQ(metadata->height, 1);
  EXPECT_EQ(metadata->capture_mode, CaptureMode::VisibleScreen);
  ASSERT_TRUE(metadata->expires_at);
  EXPECT_EQ(*metadata->expires_at - now, AutomationLimits{}.result_ttl);
  EXPECT_EQ(metadata->result_id, results.currentId(2));
}

TEST(CaptureServiceTest, ExistingInteractionOwnerMayEnterButForeignOwnerCannot) {
  CaptureEngine capture;
  ResultStore results;
  PinManager pins;
  InteractionGate gate;
  CaptureService service(capture, results, pins, gate,
      [](const ScreenPhysicalRect&, Image& image) {
        image = Image{1, 1, {1}};
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      });
  auto owner = gate.acquire(InteractionKind::Capture);
  ASSERT_TRUE(owner);
  EXPECT_TRUE(service.capture(requestFor(kGuiResultScopeId), {0, 0, 1, 1},
                              &owner).ok);
  InteractionGate other_gate;
  auto foreign = other_gate.acquire(InteractionKind::Capture);
  EXPECT_EQ(service.capture(requestFor(kGuiResultScopeId), {0, 0, 1, 1},
                            &foreign).error_code,
            ErrorCode::kBusy);
}

}  // namespace
}  // namespace qingying
