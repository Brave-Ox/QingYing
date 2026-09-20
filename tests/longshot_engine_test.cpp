#include "qingying/capture/capture_engine.hpp"
#include "qingying/longshot/longshot_engine.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace qingying {

namespace {

std::uint32_t pixelFor(int x, int global_y) {
  const std::uint32_t b = static_cast<std::uint32_t>((global_y * 17 + x) &
                                                     0xFF);
  const std::uint32_t g = static_cast<std::uint32_t>((global_y * 31 + x * 3) &
                                                     0xFF);
  const std::uint32_t r = static_cast<std::uint32_t>((global_y * 47 + x * 5) &
                                                     0xFF);
  return 0xFF000000u | (r << 16) | (g << 8) | b;
}

Image makeStrip(int width, int height, int first_global_y) {
  Image image;
  image.width = width;
  image.height = height;
  image.pixels.resize(static_cast<std::size_t>(width) *
                      static_cast<std::size_t>(height));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.pixels[static_cast<std::size_t>(y) *
                       static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] =
          pixelFor(x, first_global_y + y);
    }
  }
  return image;
}

class ScriptedLongShotProfile final : public LongShotProfile {
 public:
  explicit ScriptedLongShotProfile(bool move_on_scroll)
      : move_on_scroll_(move_on_scroll) {}

  const char* name() const noexcept override { return "test.scripted"; }

  bool resolve(const LongShotRequest& request,
               LongShotProfileResult& out) const override {
    out = LongShotProfileResult{
        1, ScreenPhysicalRect{request.x, request.y, request.width,
                              request.height}};
    return out.valid();
  }

  bool scrollDown(const LongShotRequest& /*request*/,
                  const LongShotProfileResult& /*profile*/) const override {
    ++wheel_count_;
    if (move_on_scroll_) {
      position_ = 1;
    }
    return true;
  }

  bool queryScrollState(const LongShotProfileResult& /*profile*/,
                        LongShotScrollState& out) const override {
    out.position = position_;
    out.last_position = 1;
    out.valid = true;
    return true;
  }

  int wheelCount() const noexcept { return wheel_count_; }

 private:
  bool move_on_scroll_{false};
  mutable int position_{0};
  mutable int wheel_count_{0};
};

class CancelAwareLongShotProfile final : public LongShotProfile {
 public:
  const char* name() const noexcept override { return "test.cancel-aware"; }

  bool resolve(const LongShotRequest& request,
               LongShotProfileResult& out) const override {
    out = LongShotProfileResult{
        1, ScreenPhysicalRect{request.x, request.y, request.width,
                              request.height}};
    return out.valid();
  }

  bool scrollDown(const LongShotRequest& /*request*/,
                  const LongShotProfileResult& /*profile*/) const override {
    return !cancelled_.load();
  }

  bool queryScrollState(const LongShotProfileResult& /*profile*/,
                        LongShotScrollState& out) const override {
    out.position = 1;
    out.last_position = 1;
    out.valid = true;
    return true;
  }

  void cancel() const noexcept override { cancelled_.store(true); }

  bool cancelled() const noexcept { return cancelled_.load(); }

 private:
  mutable std::atomic_bool cancelled_{false};
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

TEST(LongShotLimitsTest, DefaultsAreValidAndBounded) {
  const LongShotLimits limits;

  EXPECT_TRUE(limits.valid());
  EXPECT_EQ(limits.max_frames, 30);
  EXPECT_EQ(limits.max_output_height, 30000);
}

TEST(LongShotLimitsTest, InitialPairRequiresAtLeastTwoFrames) {
  LongShotLimits limits;
  limits.max_frames = 1;

  EXPECT_FALSE(limits.valid());
}

TEST(LongShotEngineTest, ExplicitLimitsOverrideConstructionDefaults)
{
  LongShotProfileRegistry registry;
  registry.add(std::make_unique<BottomLongShotProfile>());
  int capture_count = 0;
  LongShotEngine engine(
      [&capture_count](const ScreenPhysicalRect& region, Image& out)
      {
        ++capture_count;
        out = makeStrip(region.width, region.height, 0);
        return ActionResult{true, ErrorCode::kOk};
      },
      std::move(registry));
  const LongShotRequest request{1, 0, 0, 8, 8};
  Image out;

  const ActionResult result = engine.captureSelection(
      request, out, {}, {}, LongShotLimits{2, 5});

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kInvalidArgument);
  EXPECT_EQ(capture_count, 0);
}

TEST(LongShotEngineTest, InvalidSelectionRequestClearsOutput) {
  CaptureEngine capture;
  LongShotEngine engine(capture);
  LongShotRequest request;
  Image out;
  out.width = 1;
  out.height = 1;
  out.pixels.push_back(0xFFFFFFFFu);

  const ActionResult result = engine.captureSelection(request, out);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kInvalidArgument);
  EXPECT_TRUE(out.empty());
}

TEST(LongShotEngineTest, InvalidWindowIsUnsupported) {
  CaptureEngine capture;
  LongShotEngine engine(capture);
  LongShotRequest request;
  request.owner_window = 1;
  request.x = 100;
  request.y = 200;
  request.width = 640;
  request.height = 480;
  Image out;

  const ActionResult result = engine.captureSelection(request, out);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kLongShotUnsupported);
  EXPECT_TRUE(out.empty());
}

TEST(LongShotEngineTest, InvalidPairRequestClearsBothFrames) {
  CaptureEngine capture;
  LongShotEngine engine(capture);
  LongShotRequest request;
  LongShotFramePair frames;
  frames.first_frame = Image{1, 1, {0xFFFFFFFFu}};
  frames.second_frame = Image{1, 1, {0xFFFFFFFFu}};

  const ActionResult result = engine.captureInitialPair(request, frames);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kInvalidArgument);
  EXPECT_TRUE(frames.first_frame.empty());
  EXPECT_TRUE(frames.second_frame.empty());
}

TEST(LongShotEngineTest, CancelSignalsTheActiveProfile) {
  auto profile = std::make_unique<CancelAwareLongShotProfile>();
  CancelAwareLongShotProfile* profile_ptr = profile.get();
  LongShotProfileRegistry registry;
  registry.add(std::move(profile));

  std::mutex mutex;
  std::condition_variable condition;
  bool capture_entered = false;
  bool release_capture = false;
  LongShotEngine engine(
      [&](const ScreenPhysicalRect& region, Image& out) {
        {
          std::unique_lock<std::mutex> lock(mutex);
          capture_entered = true;
          condition.notify_all();
          condition.wait(lock, [&] { return release_capture; });
        }
        out = makeStrip(region.width, region.height, 0);
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      },
      std::move(registry));

  const LongShotRequest request{1, 0, 0, 8, 8};
  ActionResult worker_result;
  Image output;
  std::thread worker([&] {
    worker_result = engine.captureSelection(request, output);
  });

  {
    std::unique_lock<std::mutex> lock(mutex);
    ASSERT_TRUE(condition.wait_for(
        lock, std::chrono::seconds(1), [&] { return capture_entered; }));
  }

  engine.cancel();
  EXPECT_TRUE(profile_ptr->cancelled());
  {
    std::lock_guard<std::mutex> lock(mutex);
    release_capture = true;
  }
  condition.notify_all();
  worker.join();

  EXPECT_TRUE(worker_result.ok);
  EXPECT_FALSE(output.empty());
}

TEST(LongShotEngineTest, RetriesStaleFrameWithoutSendingAnotherWheel) {
  constexpr int kWidth = 32;
  constexpr int kHeight = 30;
  constexpr int kScrollDelta = 5;

  auto profile = std::make_unique<ScriptedLongShotProfile>(true);
  ScriptedLongShotProfile* profile_ptr = profile.get();
  LongShotProfileRegistry registry;
  registry.add(std::move(profile));

  int capture_count = 0;
  LongShotEngine engine(
      [&capture_count, kScrollDelta](const ScreenPhysicalRect& region,
                                     Image& out) {
        ++capture_count;
        // The first frame after scrolling is intentionally stale. The retry
        // at the same position exposes the newly rendered content.
        const int first_global_y = capture_count == 2 ? 0 :
                                    (capture_count >= 3 ? kScrollDelta : 0);
        out = makeStrip(region.width, region.height, first_global_y);
        if (capture_count >= 3) {
          // Simulate one dynamic/caret pixel in the overlap. It is outside
          // the right-edge exclusion zone but remains within the 96% match
          // budget used by the long-shot engine.
          out.pixels[12] ^= 0x00010101u;
        }
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      },
      std::move(registry));

  const LongShotRequest request{1, 0, 0, kWidth, kHeight};
  Image out;
  const ActionResult result = engine.captureSelection(request, out);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kOk);
  EXPECT_TRUE(result.failure_stage.empty());
  EXPECT_EQ(capture_count, 3);
  EXPECT_EQ(profile_ptr->wheelCount(), 1);
  EXPECT_EQ(out.width, kWidth);
  EXPECT_EQ(out.height, kHeight + kScrollDelta);
}

TEST(LongShotEngineTest,
     ReportsPersistentOverlapFailureAfterSamePositionRetries) {
  constexpr int kWidth = 32;
  constexpr int kHeight = 30;

  auto profile = std::make_unique<ScriptedLongShotProfile>(true);
  ScriptedLongShotProfile* profile_ptr = profile.get();
  LongShotProfileRegistry registry;
  registry.add(std::move(profile));

  int capture_count = 0;
  LongShotEngine engine(
      [&capture_count](const ScreenPhysicalRect& region, Image& out) {
        ++capture_count;
        out = makeStrip(region.width, region.height,
                        capture_count == 1 ? 0 : 100);
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      },
      std::move(registry));

  const LongShotRequest request{1, 0, 0, kWidth, kHeight};
  Image out;
  const ActionResult result = engine.captureSelection(request, out);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kCaptureFailed);
  EXPECT_EQ(result.failure_stage, "overlap_detection");
  EXPECT_EQ(result.failure_frame, 2);
  EXPECT_EQ(capture_count, 4);
  EXPECT_EQ(profile_ptr->wheelCount(), 1);
  EXPECT_TRUE(out.empty());
}

}  // namespace qingying
