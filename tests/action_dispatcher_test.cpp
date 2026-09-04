#include "qingying/action/action_dispatcher.hpp"
#include "qingying/action/i_action_handler.hpp"
#include "qingying/action/types.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>

namespace {

class FakeStatusHandler final : public qingying::IActionHandler {
 public:
  explicit FakeStatusHandler(int* calls = nullptr) : calls_(calls) {}

  qingying::ActionType type() const override {
    return qingying::ActionType::Status;
  }

  qingying::ActionResult handle(
      const qingying::ActionRequest& /*request*/) override {
    if (calls_ != nullptr) {
      ++*calls_;
    }
    qingying::ActionResult r;
    r.ok = true;
    r.error_code = qingying::ErrorCode::kOk;
    r.message = "ok";
    return r;
  }

 private:
  int* calls_{nullptr};
};

}  // namespace

TEST(ActionDispatcherTest, DispatchWithoutHandlerReturnsNotImplemented) {
  qingying::ActionDispatcher dispatcher;

  const qingying::ActionRequest req =
      qingying::makeActionRequest(qingying::StatusRequest{});

  const qingying::ActionResult result = dispatcher.dispatch(req);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kNotImplemented);
}

TEST(ActionDispatcherTest, RegisterNullHandlerIsIgnored) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(nullptr);

  const qingying::ActionRequest req =
      qingying::makeActionRequest(qingying::StatusRequest{});

  const qingying::ActionResult result = dispatcher.dispatch(req);
  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kNotImplemented);
}

TEST(ActionDispatcherTest, DispatchRegisteredHandlerSucceeds) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>());

  qingying::ActionRequest req =
      qingying::makeActionRequest(qingying::StatusRequest{});
  req.request_id = 11;
  req.operation_id = 22;

  const qingying::ActionResult result = dispatcher.dispatch(req);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kOk);
  EXPECT_EQ(result.message, "ok");
  EXPECT_EQ(result.request_id, 11u);
  EXPECT_EQ(result.operation_id, 22u);
}

TEST(ActionDispatcherTest, UnrelatedActionStillNotImplemented) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>());

  const qingying::ActionRequest req =
      qingying::makeActionRequest(qingying::CopyRequest{});

  const qingying::ActionResult result = dispatcher.dispatch(req);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kNotImplemented);
}

TEST(ActionRequestTest, PayloadDeterminesActionType) {
  const qingying::ActionRequest request = qingying::makeActionRequest(
      qingying::CaptureRegionRequest{
          qingying::ScreenPhysicalRect{-120, 80, 640, 480}});

  EXPECT_EQ(request.type(), qingying::ActionType::CaptureRegion);
  EXPECT_TRUE(qingying::validateActionRequest(request).valid);
}

TEST(ActionRequestTest, InvalidPayloadFailsSchemaValidation) {
  const qingying::ActionRequest request = qingying::makeActionRequest(
      qingying::CaptureRegionRequest{
          qingying::ScreenPhysicalRect{10, 20, 0, 480}});

  const qingying::ActionValidationResult validation =
      qingying::validateActionRequest(request);

  EXPECT_FALSE(validation.valid);
  EXPECT_NE(validation.message, "");
}

TEST(ActionRequestTest, ResultSelectionRequiresMatchingKindAndId) {
  const qingying::ActionRequest request = qingying::makeActionRequest(
      qingying::PinRequest{qingying::ResultSelection{
          qingying::ResultSelectionKind::Explicit,
          qingying::kInvalidResultId}});

  EXPECT_FALSE(qingying::validateActionRequest(request).valid);
}

TEST(ActionRequestTest, CorrelationIdsMustBeProvidedTogether) {
  qingying::ActionRequest request =
      qingying::makeActionRequest(qingying::StatusRequest{});
  request.request_id = 7;

  const qingying::ActionValidationResult validation =
      qingying::validateActionRequest(request);

  EXPECT_FALSE(validation.valid);
  EXPECT_NE(validation.message, "");
}

TEST(ActionRequestTest, LegacyRequestConvertsToTypedPayload) {
  qingying::LegacyActionRequest legacy;
  legacy.type = qingying::ActionType::CaptureRegion;
  legacy.x = -120;
  legacy.y = 80;
  legacy.width = 640;
  legacy.height = 480;

  const auto request = qingying::adaptLegacyActionRequest(legacy);

  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->type(), qingying::ActionType::CaptureRegion);
  const auto* payload =
      std::get_if<qingying::CaptureRegionRequest>(&request->payload);
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->region.x, -120);
  EXPECT_EQ(payload->region.width, 640);
}

TEST(ActionDispatcherTest, CancellationStopsHandlerBeforeDispatch) {
  qingying::ActionDispatcher dispatcher;
  int calls = 0;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>(&calls));

  qingying::CancellationSource source;
  qingying::ActionRequest request =
      qingying::makeActionRequest(qingying::StatusRequest{});
  request.cancellation = source.token();
  source.cancel();

  const qingying::ActionResult result = dispatcher.dispatch(request);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kCancelled);
  EXPECT_EQ(calls, 0);
}

TEST(ActionDispatcherTest, ExpiredRequestStopsHandlerBeforeDispatch) {
  qingying::ActionDispatcher dispatcher;
  int calls = 0;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>(&calls));

  qingying::ActionRequest request =
      qingying::makeActionRequest(qingying::StatusRequest{});
  request.timeout = std::chrono::milliseconds(1);
  request.submitted_at -= std::chrono::seconds(1);

  const qingying::ActionResult result = dispatcher.dispatch(request);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kTimeout);
  EXPECT_EQ(calls, 0);
}
