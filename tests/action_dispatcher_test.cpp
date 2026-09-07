#include "qingying/action/action_dispatcher.hpp"
#include "qingying/action/i_action_handler.hpp"
#include "qingying/action/i_async_action_handler.h"
#include "qingying/action/types.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <utility>

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

class CancelDuringStatusHandler final : public qingying::IActionHandler {
 public:
  explicit CancelDuringStatusHandler(qingying::CancellationSource& source)
      : source_(source) {}

  qingying::ActionType type() const override {
    return qingying::ActionType::Status;
  }

  qingying::ActionResult handle(
      const qingying::ActionRequest& /*request*/) override {
    source_.cancel();
    qingying::ActionResult result;
    result.ok = true;
    result.error_code = qingying::ErrorCode::kOk;
    result.message = "committed";
    return result;
  }

 private:
  qingying::CancellationSource& source_;
};

class ThrowingStatusHandler final : public qingying::IActionHandler {
 public:
  qingying::ActionType type() const override {
    return qingying::ActionType::Status;
  }

  qingying::ActionResult handle(
      const qingying::ActionRequest& /*request*/) override {
    throw std::runtime_error("test handler failure");
  }
};

class SelfCancelledStatusHandler final : public qingying::IActionHandler {
 public:
  qingying::ActionType type() const override {
    return qingying::ActionType::Status;
  }

  qingying::ActionResult handle(
      const qingying::ActionRequest& /*request*/) override {
    qingying::ActionResult result;
    result.ok = false;
    result.error_code = qingying::ErrorCode::kCancelled;
    result.message = "handler cancelled";
    return result;
  }
};

class FakeAsyncStatusHandler final : public qingying::IAsyncActionHandler {
 public:
  enum class Behavior {
    Defer,
    CompleteInline,
    CompleteThenThrow,
    ThrowBeforeCompletion,
  };

  explicit FakeAsyncStatusHandler(Behavior behavior = Behavior::Defer)
      : behavior_(behavior) {}

  qingying::ActionType type() const override {
    return qingying::ActionType::Status;
  }

  void handleAsync(const qingying::ActionRequest& request,
                  qingying::ActionCompletion completion,
                  const qingying::ActionExecutor& executor) override {
    ++calls;
    observed_submitted_at = request.submitted_at;
    if (behavior_ == Behavior::ThrowBeforeCompletion) {
      throw std::runtime_error("test async setup failure");
    }

    completion_ = std::move(completion);
    if (behavior_ == Behavior::CompleteInline) {
      completeOnce();
    } else if (behavior_ == Behavior::CompleteThenThrow) {
      completeOnce();
      throw std::runtime_error("test async post-completion failure");
    } else if (executor && schedule_with_executor) {
      executor([this] { completeOnce(); });
    }
  }

  void completeOnce() {
    if (completion_) {
      qingying::ActionResult result;
      result.ok = true;
      result.error_code = qingying::ErrorCode::kOk;
      result.message = "async committed";
      completion_(result);
    }
  }

  void completeTwice() {
    completeOnce();
    completeOnce();
  }

  int calls{0};
  bool schedule_with_executor{false};
  std::chrono::steady_clock::time_point observed_submitted_at{};

 private:
  Behavior behavior_;
  qingying::ActionCompletion completion_;
};

qingying::ActionRequest correlatedStatusRequest() {
  qingying::ActionRequest request =
      qingying::makeActionRequest(qingying::StatusRequest{});
  request.request_id = 101;
  request.operation_id = 202;
  return request;
}

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

TEST(ActionDispatcherTest, DispatchPreservesCommittedResultAfterCancellation) {
  qingying::CancellationSource source;
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<CancelDuringStatusHandler>(source));

  qingying::ActionRequest request = correlatedStatusRequest();
  request.cancellation = source.token();

  const qingying::ActionResult result = dispatcher.dispatch(request);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kOk);
  EXPECT_EQ(result.message, "committed");
  EXPECT_EQ(result.request_id, 101u);
  EXPECT_EQ(result.operation_id, 202u);
}

TEST(ActionDispatcherTest, DispatchHandlerExceptionBecomesCorrelatedFailure) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<ThrowingStatusHandler>());

  const qingying::ActionResult result =
      dispatcher.dispatch(correlatedStatusRequest());

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kUnknown);
  EXPECT_EQ(result.request_id, 101u);
  EXPECT_EQ(result.operation_id, 202u);
}

TEST(ActionDispatcherTest, DispatchPreservesHandlerCancellationResult) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<SelfCancelledStatusHandler>());

  const qingying::ActionResult result =
      dispatcher.dispatch(correlatedStatusRequest());

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kCancelled);
  EXPECT_EQ(result.message, "handler cancelled");
  EXPECT_EQ(result.request_id, 101u);
  EXPECT_EQ(result.operation_id, 202u);
}

TEST(ActionDispatcherTest, SubmitAdaptsSynchronousHandlerAndCompletesOnce) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>());

  int completions = 0;
  qingying::ActionResult received;
  dispatcher.submit(correlatedStatusRequest(), [&](qingying::ActionResult result) {
    ++completions;
    received = std::move(result);
  });

  EXPECT_EQ(completions, 1);
  EXPECT_TRUE(received.ok);
  EXPECT_EQ(received.error_code, qingying::ErrorCode::kOk);
  EXPECT_EQ(received.request_id, 101u);
  EXPECT_EQ(received.operation_id, 202u);
}

TEST(ActionDispatcherTest, SubmitPreservesHandlerCancellationResult) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<SelfCancelledStatusHandler>());

  int completions = 0;
  qingying::ActionResult received;
  dispatcher.submit(correlatedStatusRequest(), [&](qingying::ActionResult result) {
    ++completions;
    received = std::move(result);
  });

  EXPECT_EQ(completions, 1);
  EXPECT_FALSE(received.ok);
  EXPECT_EQ(received.error_code, qingying::ErrorCode::kCancelled);
  EXPECT_EQ(received.request_id, 101u);
  EXPECT_EQ(received.operation_id, 202u);
}

TEST(ActionDispatcherTest, SubmitRejectsCancellationBeforeHandlerRuns) {
  qingying::ActionDispatcher dispatcher;
  int calls = 0;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>(&calls));

  qingying::CancellationSource source;
  source.cancel();
  qingying::ActionRequest request = correlatedStatusRequest();
  request.cancellation = source.token();

  int completions = 0;
  qingying::ActionResult received;
  dispatcher.submit(request, [&](qingying::ActionResult result) {
    ++completions;
    received = std::move(result);
  });

  EXPECT_EQ(completions, 1);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(received.error_code, qingying::ErrorCode::kCancelled);
  EXPECT_EQ(received.request_id, 101u);
  EXPECT_EQ(received.operation_id, 202u);
}

TEST(ActionDispatcherTest, SubmitRejectsExpiredRequestWithoutResettingDeadline) {
  qingying::ActionDispatcher dispatcher;
  int calls = 0;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>(&calls));

  qingying::ActionRequest request = correlatedStatusRequest();
  request.timeout = std::chrono::milliseconds(1);
  request.submitted_at -= std::chrono::seconds(1);

  int completions = 0;
  qingying::ActionResult received;
  dispatcher.submit(request, [&](qingying::ActionResult result) {
    ++completions;
    received = std::move(result);
  });

  EXPECT_EQ(completions, 1);
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(received.error_code, qingying::ErrorCode::kTimeout);
}

TEST(ActionDispatcherTest, SubmitInvalidRequestCompletesWithValidationFailure) {
  qingying::ActionDispatcher dispatcher;
  qingying::ActionRequest request = correlatedStatusRequest();
  request.context.result_scope = qingying::kInvalidResultScopeId;

  int completions = 0;
  qingying::ActionResult received;
  dispatcher.submit(request, [&](qingying::ActionResult result) {
    ++completions;
    received = std::move(result);
  });

  EXPECT_EQ(completions, 1);
  EXPECT_EQ(received.error_code, qingying::ErrorCode::kInvalidArgument);
  EXPECT_EQ(received.request_id, 101u);
  EXPECT_EQ(received.operation_id, 202u);
}

TEST(ActionDispatcherTest, SubmitMissingHandlerCompletesWithNotImplemented) {
  qingying::ActionDispatcher dispatcher;
  int completions = 0;
  qingying::ActionResult received;
  dispatcher.submit(correlatedStatusRequest(), [&](qingying::ActionResult result) {
    ++completions;
    received = std::move(result);
  });

  EXPECT_EQ(completions, 1);
  EXPECT_EQ(received.error_code, qingying::ErrorCode::kNotImplemented);
  EXPECT_EQ(received.request_id, 101u);
  EXPECT_EQ(received.operation_id, 202u);
}

TEST(ActionDispatcherTest, SubmitUsesIndependentAsyncRegistration) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerHandler(std::make_unique<FakeStatusHandler>());
  auto async_handler = std::make_unique<FakeAsyncStatusHandler>();
  FakeAsyncStatusHandler* async_handler_ptr = async_handler.get();
  dispatcher.registerAsyncHandler(std::move(async_handler));

  const qingying::ActionResult synchronous =
      dispatcher.dispatch(correlatedStatusRequest());
  EXPECT_TRUE(synchronous.ok);
  EXPECT_EQ(synchronous.message, "ok");
  EXPECT_EQ(async_handler_ptr->calls, 0);

  int completions = 0;
  dispatcher.submit(correlatedStatusRequest(), [&](qingying::ActionResult result) {
    ++completions;
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.message, "async committed");
    EXPECT_EQ(result.request_id, 101u);
    EXPECT_EQ(result.operation_id, 202u);
  });
  EXPECT_EQ(async_handler_ptr->calls, 1);
  EXPECT_EQ(completions, 0);

  async_handler_ptr->completeOnce();
  EXPECT_EQ(completions, 1);
}

TEST(ActionDispatcherTest, SubmitAsyncHandlerMayCompleteInline) {
  qingying::ActionDispatcher dispatcher;
  auto async_handler = std::make_unique<FakeAsyncStatusHandler>(
      FakeAsyncStatusHandler::Behavior::CompleteInline);
  dispatcher.registerAsyncHandler(std::move(async_handler));

  int completions = 0;
  dispatcher.submit(correlatedStatusRequest(), [&](qingying::ActionResult result) {
    ++completions;
    EXPECT_TRUE(result.ok);
  });

  EXPECT_EQ(completions, 1);
}

TEST(ActionDispatcherTest, DispatchDoesNotTreatAsyncHandlerAsSynchronous) {
  qingying::ActionDispatcher dispatcher;
  auto async_handler = std::make_unique<FakeAsyncStatusHandler>();
  dispatcher.registerAsyncHandler(std::move(async_handler));

  const qingying::ActionResult result =
      dispatcher.dispatch(correlatedStatusRequest());

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, qingying::ErrorCode::kNotImplemented);
  EXPECT_EQ(result.request_id, 101u);
  EXPECT_EQ(result.operation_id, 202u);
}

TEST(ActionDispatcherTest, SubmitSuppressesDuplicateAsyncCompletion) {
  qingying::ActionDispatcher dispatcher;
  auto async_handler = std::make_unique<FakeAsyncStatusHandler>();
  FakeAsyncStatusHandler* async_handler_ptr = async_handler.get();
  dispatcher.registerAsyncHandler(std::move(async_handler));

  int completions = 0;
  dispatcher.submit(correlatedStatusRequest(), [&](qingying::ActionResult result) {
    ++completions;
    EXPECT_TRUE(result.ok);
  });
  async_handler_ptr->completeTwice();

  EXPECT_EQ(completions, 1);
}

TEST(ActionDispatcherTest, SubmitAsyncHandlerExceptionCompletesWithFailure) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerAsyncHandler(std::make_unique<FakeAsyncStatusHandler>(
      FakeAsyncStatusHandler::Behavior::ThrowBeforeCompletion));

  int completions = 0;
  qingying::ActionResult received;
  dispatcher.submit(correlatedStatusRequest(), [&](qingying::ActionResult result) {
    ++completions;
    received = std::move(result);
  });

  EXPECT_EQ(completions, 1);
  EXPECT_EQ(received.error_code, qingying::ErrorCode::kUnknown);
  EXPECT_EQ(received.request_id, 101u);
  EXPECT_EQ(received.operation_id, 202u);
}

TEST(ActionDispatcherTest, AsyncCompletionWinsIfHandlerThrowsAfterCompletion) {
  qingying::ActionDispatcher dispatcher;
  dispatcher.registerAsyncHandler(std::make_unique<FakeAsyncStatusHandler>(
      FakeAsyncStatusHandler::Behavior::CompleteThenThrow));

  int completions = 0;
  qingying::ActionResult received;
  dispatcher.submit(correlatedStatusRequest(), [&](qingying::ActionResult result) {
    ++completions;
    received = std::move(result);
  });

  EXPECT_EQ(completions, 1);
  EXPECT_TRUE(received.ok);
  EXPECT_EQ(received.message, "async committed");
}

TEST(ActionDispatcherTest, AsyncCompletionPreservesOriginalSubmittedAt) {
  qingying::ActionDispatcher dispatcher;
  auto async_handler = std::make_unique<FakeAsyncStatusHandler>();
  FakeAsyncStatusHandler* async_handler_ptr = async_handler.get();
  dispatcher.registerAsyncHandler(std::move(async_handler));

  qingying::ActionRequest request = correlatedStatusRequest();
  request.timeout = std::chrono::seconds(10);
  request.submitted_at -= std::chrono::seconds(3);
  const auto submitted_at = request.submitted_at;

  dispatcher.submit(request, [](qingying::ActionResult /*result*/) {});

  EXPECT_EQ(async_handler_ptr->observed_submitted_at, submitted_at);
}

TEST(ActionDispatcherTest, InjectedExecutorIsPassedToAsyncHandler) {
  bool executor_called = false;
  qingying::ActionDispatcher dispatcher([&](qingying::ActionTask task) {
    executor_called = true;
    task();
  });
  auto async_handler = std::make_unique<FakeAsyncStatusHandler>();
  async_handler->schedule_with_executor = true;
  dispatcher.registerAsyncHandler(std::move(async_handler));

  int completions = 0;
  dispatcher.submit(correlatedStatusRequest(), [&](qingying::ActionResult result) {
    ++completions;
    EXPECT_TRUE(result.ok);
  });

  EXPECT_TRUE(executor_called);
  EXPECT_EQ(completions, 1);
}
