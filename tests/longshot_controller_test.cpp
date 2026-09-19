#include "qingying/app/longshot_controller.hpp"

#include "qingying/app/longshot_limits_provider.hpp"
#include "qingying/capture/capture_engine.hpp"

#include <Windows.h>

#include <gtest/gtest.h>
#include <chrono>
#include <thread>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vector>

namespace qingying {
namespace {

struct ControllerFixture {
  CaptureEngine capture;
  LongShotEngine engine{capture};
  SelectionOverlay overlay;
  LongShotLimitsProvider limits_provider;
  LongShotController controller{engine, overlay, limits_provider};
};

class BottomLongShotProfile final : public LongShotProfile
{
 public:
  const char* name() const noexcept override
  {
    return "test.bottom";
  }

  bool resolve(const LongShotRequest& request,
               LongShotProfileResult& out) const override
  {
    out = LongShotProfileResult{
        1, ScreenPhysicalRect{request.x, request.y, request.width,
                              request.height}};
    return out.valid();
  }

  bool scrollDown(const LongShotRequest&,
                  const LongShotProfileResult&) const override
  {
    return true;
  }

  bool queryScrollState(const LongShotProfileResult&,
                        LongShotScrollState& out) const override
  {
    out = LongShotScrollState{1, 1, true};
    return true;
  }
};

}  // namespace

TEST(LongShotControllerTest, StartsInactive) {
  ControllerFixture fixture;

  EXPECT_FALSE(fixture.controller.active());
}

TEST(LongShotControllerTest, RejectsInvalidRequestWithoutStartingWorker) {
  ControllerFixture fixture;

  EXPECT_FALSE(fixture.controller.start(LongShotRequest{}));
  EXPECT_FALSE(fixture.controller.active());
}

TEST(LongShotControllerTest, CancelAndJoinAreSafeWhenIdle) {
  ControllerFixture fixture;

  fixture.controller.cancel();
  fixture.controller.join();

  EXPECT_FALSE(fixture.controller.active());
}

TEST(LongShotControllerTest, CancelAndJoinStopAStartedWorker) {
  ControllerFixture fixture;
  fixture.controller.setOwnerWindow(reinterpret_cast<HWND>(1));

  const LongShotRequest request{1, 0, 0, 1, 1};
  ASSERT_TRUE(fixture.controller.start(request));

  fixture.controller.cancel();
  fixture.controller.join();

  EXPECT_FALSE(fixture.controller.active());
}

TEST(LongShotControllerTest, ShutdownIsIdempotentAndRejectsFutureStarts) {
  ControllerFixture fixture;

  fixture.controller.shutdown();
  fixture.controller.shutdown();

  const LongShotRequest request{1, 0, 0, 100, 100};
  EXPECT_FALSE(fixture.controller.start(request));
  EXPECT_FALSE(fixture.controller.active());
}

TEST(LongShotControllerTest, InvalidCompletionTokenIsSafe) {
  ControllerFixture fixture;
  ActionResult result;
  Image image;

  EXPECT_FALSE(fixture.controller.handleCompletion(
      kInvalidUiMessageToken, result, image));
  fixture.controller.shutdown();
  fixture.controller.shutdown();
}

TEST(LongShotControllerTest, StartUsesLimitsSnapshotTakenBeforeWorkerRuns)
{
  LongShotLimitsProvider limits_provider{LongShotLimits{2, 5}};
  LongShotProfileRegistry profiles;
  profiles.add(std::make_unique<BottomLongShotProfile>());
  std::atomic<int> capture_calls{0};
  LongShotEngine engine(
      [&capture_calls](const ScreenPhysicalRect& region, Image& out)
      {
        capture_calls.fetch_add(1);
        out = Image{region.width, region.height,
                    std::vector<std::uint32_t>(
                        static_cast<std::size_t>(region.width) *
                            static_cast<std::size_t>(region.height),
                        0xFFFFFFFFu)};
        return ActionResult{true, ErrorCode::kOk};
      },
      std::move(profiles));
  SelectionOverlay overlay;
  LongShotController controller{engine, overlay, limits_provider};
  controller.setOwnerWindow(reinterpret_cast<HWND>(1));

  ASSERT_TRUE(controller.start(LongShotRequest{1, 0, 0, 8, 8}));
  controller.join();

  EXPECT_EQ(capture_calls.load(), 0);
}

TEST(LongShotControllerTest, UpdatingProviderDuringCaptureDoesNotChangeTaskLimits)
{
  LongShotLimitsProvider limits_provider{LongShotLimits{2, 20}};
  LongShotProfileRegistry profiles;
  profiles.add(std::make_unique<BottomLongShotProfile>());
  std::mutex mutex;
  std::condition_variable condition;
  bool capture_entered = false;
  bool release_capture = false;
  std::atomic<int> capture_calls{0};
  LongShotEngine engine(
      [&mutex, &condition, &capture_entered, &release_capture,
       &capture_calls](const ScreenPhysicalRect& region, Image& out)
      {
        capture_calls.fetch_add(1);
        {
          std::unique_lock<std::mutex> lock(mutex);
          capture_entered = true;
          condition.notify_all();
          condition.wait(lock, [&release_capture]
          {
            return release_capture;
          });
        }
        out = Image{region.width, region.height,
                    std::vector<std::uint32_t>(
                        static_cast<std::size_t>(region.width) *
                            static_cast<std::size_t>(region.height),
                        0xFFFFFFFFu)};
        return ActionResult{true, ErrorCode::kOk};
      },
      std::move(profiles));
  SelectionOverlay overlay;
  LongShotController controller{engine, overlay, limits_provider};
  controller.setOwnerWindow(reinterpret_cast<HWND>(1));

  ASSERT_TRUE(controller.start(LongShotRequest{1, 0, 0, 8, 8}));
  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(
        lock, std::chrono::seconds(1),
        [&capture_entered]
        {
          return capture_entered;
        }));
  }
  limits_provider.update(LongShotLimits{2, 5});
  {
    std::lock_guard<std::mutex> lock(mutex);
    release_capture = true;
  }
  condition.notify_all();
  controller.join();

  EXPECT_EQ(capture_calls.load(), 1);
}

namespace {
class CompletionProfile final : public LongShotProfile {
 public:
  bool at_bottom{true};
  const char* name() const noexcept override { return "test.completion"; }
  bool resolve(const LongShotRequest& request,
               LongShotProfileResult& out) const override {
    out = LongShotProfileResult{1, request.selectionRect()};
    return true;
  }
  bool scrollDown(const LongShotRequest&,
                  const LongShotProfileResult&) const override { return true; }
  bool queryScrollState(const LongShotProfileResult&,
                        LongShotScrollState& out) const override {
    out = LongShotScrollState{0, 0, at_bottom};
    return at_bottom;
  }
};

struct CompletionFixture {
  HWND window{CreateWindowExW(0, L"STATIC", L"longshot-completion-test", 0,
                              0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr)};
  SelectionOverlay overlay;
  LongShotLimitsProvider limits_provider;
  CompletionProfile* profile{nullptr};
  LongShotCaptureCallback capture = [](const ScreenPhysicalRect& rect, Image& out) {
    out = Image{rect.width, rect.height, {}};
    out.pixels.assign(static_cast<std::size_t>(rect.width) * rect.height,
                      0xFF123456u);
    ActionResult result;
    result.ok = true;
    result.error_code = ErrorCode::kOk;
    return result;
  };
  LongShotProfileRegistry profiles() {
    LongShotProfileRegistry registry;
    auto item = std::make_unique<CompletionProfile>();
    profile = item.get();
    registry.add(std::move(item));
    return registry;
  }
  LongShotEngine engine{[this](const ScreenPhysicalRect& rect, Image& out) {
    return capture(rect, out);
  }, profiles()};
  LongShotController controller{engine, overlay, limits_provider};
  CompletionFixture() { controller.setOwnerWindow(window); }
  ~CompletionFixture() {
    controller.shutdown();
    MSG message{};
    while (PeekMessageW(&message, window, WM_QINGYING_LONGSHOT_COMPLETE,
                        WM_QINGYING_LONGSHOT_COMPLETE, PM_REMOVE)) {}
    DestroyWindow(window);
  }
  UiMessageToken token() {
    MSG message{};
    if (!PeekMessageW(&message, window, WM_QINGYING_LONGSHOT_COMPLETE,
                     WM_QINGYING_LONGSHOT_COMPLETE, PM_REMOVE)) return 0;
    return static_cast<UiMessageToken>(message.lParam);
  }
  bool start() { return controller.start({1, 0, 0, 32, 40}); }
};
}

TEST(LongShotControllerTest, DeliversFullOutcomeAndConsumesCompletionOnlyOnce) {
  CompletionFixture fixture;
  ASSERT_NE(fixture.window, nullptr);
  ASSERT_TRUE(fixture.overlay.show(Image{}, [](const SelectionResult&) {}));
  ASSERT_TRUE(fixture.start());
  fixture.controller.join();
  const auto token = fixture.token();
  ASSERT_NE(token, 0u);
  LongShotOutcome outcome;
  ASSERT_TRUE(fixture.controller.handleCompletion(token, outcome));
  EXPECT_EQ(outcome.strategy, "test.completion");
  EXPECT_EQ(outcome.stop_reason, LongShotStopReason::ReachedBottom);
  EXPECT_EQ(outcome.accepted_frames, 1);
  EXPECT_TRUE(outcome.isComplete());
  EXPECT_EQ(outcome.image.pixels.size(), 1280u);
  // 完成通知先被 UI 接收时，尚未显示的最终预览仍然可用。
  EXPECT_GT(ImageMemoryBudget::global().snapshot().bytes[
                static_cast<std::size_t>(ImageMemoryKind::Preview)], 0u);
  EXPECT_FALSE(fixture.controller.handleCompletion(token, outcome));
}

TEST(LongShotControllerTest, CaptureFailureStillDeliversVerifiedImage) {
  CompletionFixture fixture;
  fixture.profile->at_bottom = false;
  const auto capture = fixture.capture;
  int captures = 0;
  fixture.capture = [&](const ScreenPhysicalRect& rect, Image& out) {
    if (++captures == 1) return capture(rect, out);
    ActionResult result;
    result.error_code = ErrorCode::kCaptureFailed;
    return result;
  };
  ASSERT_TRUE(fixture.start());
  fixture.controller.join();
  LongShotOutcome outcome;
  ASSERT_TRUE(fixture.controller.handleCompletion(fixture.token(), outcome));
  EXPECT_EQ(outcome.stop_reason, LongShotStopReason::CaptureFailed);
  EXPECT_EQ(outcome.failure_stage, LongShotFailureStage::FrameCapture);
  EXPECT_EQ(outcome.diagnostic.failure_frame, 2);
  EXPECT_FALSE(outcome.diagnostic.ok);
  EXPECT_EQ(outcome.accepted_frames, 1);
  EXPECT_TRUE(outcome.hasExportableResult());
  EXPECT_EQ(outcome.image.pixels.front(), 0xFF123456u);
}

TEST(LongShotControllerTest, OldTokenDoesNotJoinNewWorker) {
  CompletionFixture fixture;
  ASSERT_TRUE(fixture.start());
  fixture.controller.join();
  const auto old_token = fixture.token();
  ASSERT_NE(old_token, 0u);
  const auto capture = fixture.capture;
  fixture.capture = [&](const ScreenPhysicalRect& rect, Image& out) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return capture(rect, out);
  };
  ASSERT_TRUE(fixture.start());
  LongShotOutcome outcome;
  const auto before = std::chrono::steady_clock::now();
  EXPECT_FALSE(fixture.controller.handleCompletion(old_token, outcome));
  EXPECT_LT(std::chrono::steady_clock::now() - before,
            std::chrono::milliseconds(200));
  fixture.controller.join();
  EXPECT_TRUE(fixture.controller.handleCompletion(fixture.token(), outcome));
  EXPECT_TRUE(outcome.isComplete());
}

TEST(LongShotControllerTest, CancelAfterWorkerFinishedSuppressesQueuedImage) {
  CompletionFixture fixture;
  ASSERT_TRUE(fixture.start());
  fixture.controller.join();
  fixture.controller.cancel();
  LongShotOutcome outcome;
  ASSERT_TRUE(fixture.controller.handleCompletion(fixture.token(), outcome));
  EXPECT_EQ(outcome.stop_reason, LongShotStopReason::Cancelled);
  EXPECT_EQ(outcome.diagnostic.error_code, ErrorCode::kCancelled);
  EXPECT_TRUE(outcome.image.empty());
}

TEST(LongShotControllerTest, StopPreservesFrameAndShutdownHonorsDeadline) {
  for (bool shutdown : {false, true}) {
    CompletionFixture fixture;
    const auto capture = fixture.capture;
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false, release = false;
    fixture.capture = [&](const ScreenPhysicalRect& rect, Image& out) {
      std::unique_lock<std::mutex> lock(mutex);
      entered = true;
      condition.notify_all();
      condition.wait(lock, [&] { return release; });
      return capture(rect, out);
    };
    ASSERT_TRUE(fixture.start());
    {
      std::unique_lock<std::mutex> lock(mutex);
      const bool ready = condition.wait_for(lock, std::chrono::seconds(2),
                                            [&] { return entered; });
      EXPECT_TRUE(ready);
    }
    fixture.controller.handleControl(LongShotControl::TogglePause);
    if (shutdown) {
      fixture.controller.beginShutdown();
      EXPECT_FALSE(fixture.controller.joinUntil(std::chrono::steady_clock::now()));
    } else {
      fixture.controller.handleControl(LongShotControl::Stop);
    }
    {
      std::lock_guard<std::mutex> lock(mutex);
      release = true;
    }
    condition.notify_all();
    fixture.controller.join();
    LongShotOutcome outcome;
    if (shutdown) {
      EXPECT_EQ(fixture.token(), 0u);
      fixture.controller.finishShutdown();
    } else {
      ASSERT_TRUE(fixture.controller.handleCompletion(fixture.token(), outcome));
      EXPECT_EQ(outcome.stop_reason, LongShotStopReason::UserStopped);
      EXPECT_EQ(outcome.accepted_frames, 1);
      EXPECT_TRUE(outcome.hasExportableResult());
    }
  }
}

}  // namespace qingying
