#include "qingying/action/i_action_handler.hpp"
#include "qingying/mcp/mcp_bridge.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace {

class FakeStatusHandler final : public qingying::IActionHandler {
 public:
  qingying::ActionType type() const override {
    return qingying::ActionType::Status;
  }

  qingying::ActionResult handle(
      const qingying::ActionRequest& /*request*/) override {
    qingying::ActionResult result;
    result.ok = true;
    result.error_code = qingying::ErrorCode::kOk;
    result.message = "ok";
    return result;
  }
};

}  // namespace

TEST(McpBridgeTest, RejectsSubmissionWithoutDispatcher) {
  qingying::McpBridge bridge(nullptr);
  const qingying::ActionRequest request =
      qingying::makeActionRequest(qingying::StatusRequest{});

  const qingying::ActionResult result = bridge.submit(request);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kNotReady);
}

TEST(McpBridgeTest, ValidatesTypedRequestBeforeForwarding) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>());
  qingying::McpBridge bridge(&dispatcher);

  const qingying::ActionRequest request = qingying::makeActionRequest(
      qingying::CaptureRegionRequest{
          qingying::ScreenPhysicalRect{0, 0, 0, 480}});

  const qingying::ActionResult result = bridge.submit(request);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kInvalidArgument);
}

TEST(McpBridgeTest, ForwardsCorrelationIdsToDispatcherResult) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>());
  qingying::McpBridge bridge(&dispatcher);

  qingying::ActionRequest request =
      qingying::makeActionRequest(qingying::StatusRequest{});
  request.request_id = 101;
  request.operation_id = 202;

  const qingying::ActionResult result = bridge.submit(request);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.request_id, 101u);
  EXPECT_EQ(result.operation_id, 202u);
}
