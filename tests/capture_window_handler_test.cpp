#include "qingying/app/action_handlers.hpp"
#include "qingying/app/capture_service.h"
#include "qingying/app/result_action_service.h"
#include "qingying/app/result_store.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <gtest/gtest.h>

namespace qingying {
namespace {
WindowCatalogEntry window(std::uintptr_t handle, std::uint32_t pid,
                          std::wstring title,
                          ScreenPhysicalRect bounds = {10, 20, 3, 2}) {
  return {handle, pid, std::move(title), bounds};
}

class CaptureWindowHandlerTest : public ::testing::Test {
 protected:
  ActionDispatcher dispatcher;
  CaptureEngine capture;
  ExportService exporter;
  ResultStore results;
  PinManager pins;
  InteractionGate gate;
  ResultActionService actions{results, exporter, pins};
  WindowCatalogSnapshot snapshot{{window(7, 42, L"轻映测试窗口")}};
  ScreenPhysicalRect captured{};
  int captures{0};
  CaptureService service{
      capture, results, pins, gate,
      [this](const ScreenPhysicalRect& bounds, Image& image) {
        captured = bounds;
        ++captures;
        EXPECT_TRUE(pins.captureExclusionActive());
        image = Image{bounds.width, bounds.height,
                      std::vector<std::uint32_t>(
                          static_cast<std::size_t>(bounds.width) *
                          static_cast<std::size_t>(bounds.height), 0xFF112233)};
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      }, {}, [this] { return snapshot; }};

  void SetUp() override {
    registerAppHandlers(dispatcher, service, results, actions);
  }

  ActionRequest request(CaptureWindowRequest payload = {L"测试"}) {
    auto value = makeActionRequest(std::move(payload));
    value.context.result_scope = 9;
    return value;
  }
};

TEST_F(CaptureWindowHandlerTest, CapturesUniqueVisibleBoundsIntoRequestScope) {
  const auto result = dispatcher.dispatch(request());
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(captured, (ScreenPhysicalRect{10, 20, 3, 2}));
  EXPECT_EQ(captures, 1);
  EXPECT_FALSE(pins.captureExclusionActive());
  const auto* output = std::get_if<CapturedResult>(&result.output);
  ASSERT_NE(output, nullptr);
  EXPECT_EQ(output->bounds, captured);
  EXPECT_EQ(output->capture_mode, CaptureMode::VisibleScreen);
  EXPECT_TRUE(results.acquire(9, output->result_id));
  EXPECT_FALSE(results.acquire(10, output->result_id));
}

TEST_F(CaptureWindowHandlerTest, AmbiguousQueryReturnsBoundedCandidates) {
  snapshot.entries.push_back(window(8, 43, L"另一个测试窗口"));
  const auto old = results.publish(9, Image{1, 1, {1}});
  const auto result = dispatcher.dispatch(request());
  EXPECT_EQ(result.error_code, ErrorCode::kWindowAmbiguous);
  const auto* candidates = std::get_if<WindowCandidates>(&result.output);
  ASSERT_NE(candidates, nullptr);
  EXPECT_EQ(candidates->candidates.size(), 2u);
  EXPECT_EQ(captures, 0);
  EXPECT_TRUE(results.acquire(9, old));
}

TEST_F(CaptureWindowHandlerTest, MissingWindowDoesNotReplaceOldResult) {
  snapshot.entries.clear();
  const auto old = results.publish(9, Image{1, 1, {1}});
  const auto result = dispatcher.dispatch(request());
  EXPECT_EQ(result.error_code, ErrorCode::kWindowNotFound);
  EXPECT_EQ(captures, 0);
  EXPECT_TRUE(results.acquire(9, old));
}

TEST_F(CaptureWindowHandlerTest, RevalidationRejectsMoveCloseAndHandleReuse) {
  int reads = 0;
  WindowCatalogSnapshot first{{window(7, 42, L"target")}};
  WindowCatalogSnapshot changed{{window(7, 42, L"target", {11, 20, 3, 2})}};
  CaptureService moving_service{capture, results, pins, gate,
      [&](const ScreenPhysicalRect&, Image&) { ++captures; return ActionResult{}; },
      {}, [&] { return reads++ == 0 ? first : changed; }};
  ActionDispatcher moving_dispatcher;
  registerAppHandlers(moving_dispatcher, moving_service, results, actions);
  EXPECT_EQ(moving_dispatcher.dispatch(request({L"target", WindowMatchMode::Exact, 42})).error_code,
            ErrorCode::kWindowNotFound);

  reads = 0;
  changed.entries[0].bounds = first.entries[0].bounds;
  changed.entries[0].process_id = 99;
  EXPECT_EQ(moving_dispatcher.dispatch(request({L"target", WindowMatchMode::Exact, 42})).error_code,
            ErrorCode::kWindowNotFound);

  reads = 0;
  changed.entries.clear();
  EXPECT_EQ(moving_dispatcher.dispatch(request({L"target", WindowMatchMode::Exact, 42})).error_code,
            ErrorCode::kWindowNotFound);
  EXPECT_EQ(captures, 0);
}
}  // namespace
}  // namespace qingying
