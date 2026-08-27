#include "qingying/action/action_dispatcher.hpp"
#include "qingying/action/i_action_handler.hpp"
#include "qingying/action/types.hpp"

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
    qingying::ActionResult r;
    r.ok = true;
    r.error_code = qingying::ErrorCode::kOk;
    r.message = "ok";
    return r;
  }
};

}  // namespace

TEST(ActionDispatcherTest, DispatchWithoutHandlerReturnsNotImplemented) {
  qingying::ActionDispatcher dispatcher;

  qingying::ActionRequest req;
  req.type = qingying::ActionType::Status;

  const qingying::ActionResult result = dispatcher.dispatch(req);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kNotImplemented);
}

TEST(ActionDispatcherTest, RegisterNullHandlerIsIgnored) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(nullptr);

  qingying::ActionRequest req;
  req.type = qingying::ActionType::Status;

  const qingying::ActionResult result = dispatcher.dispatch(req);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kNotImplemented);
}

TEST(ActionDispatcherTest, DispatchRegisteredHandlerSucceeds) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>());

  qingying::ActionRequest req;
  req.type = qingying::ActionType::Status;

  const qingying::ActionResult result = dispatcher.dispatch(req);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kOk);
  EXPECT_EQ(result.message, "ok");
}

TEST(ActionDispatcherTest, UnrelatedActionStillNotImplemented) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>());

  qingying::ActionRequest req;
  req.type = qingying::ActionType::Copy;

  const qingying::ActionResult result = dispatcher.dispatch(req);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kNotImplemented);
}
