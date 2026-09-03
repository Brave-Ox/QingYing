#include "qingying/app/longshot_controller.hpp"

#include "qingying/capture/capture_engine.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

namespace qingying {
namespace {

struct ControllerFixture {
  CaptureEngine capture;
  LongShotEngine engine{capture};
  SelectionOverlay overlay;
  LongShotController controller{engine, overlay};
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

}  // namespace qingying
