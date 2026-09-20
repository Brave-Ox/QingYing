#include "qingying/app/longshot_controller.hpp"

#include "qingying/app/longshot_limits_provider.hpp"
#include "qingying/capture/capture_engine.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

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

}  // namespace qingying
