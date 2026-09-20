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

Image makePeriodicStrip(int width, int height, int period) {
  Image image = makeStrip(width, height, 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.pixels[static_cast<std::size_t>(y) * width + x] =
          pixelFor(x, y % period);
    }
  }
  return image;
}

Image makeFixedEdgeFrame(int width, int height, int top_rows,
                         int bottom_rows, int first_body_row,
                         std::uint32_t top_pixel,
                         std::uint32_t bottom_pixel) {
  Image image;
  image.width = width;
  image.height = height;
  image.pixels.resize(static_cast<std::size_t>(width) * height);
  const int body_rows = height - top_rows - bottom_rows;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      image.pixels[static_cast<std::size_t>(y) * width + x] =
          y < top_rows
              ? top_pixel
              : y >= top_rows + body_rows
                    ? bottom_pixel
                    : pixelFor(x, first_body_row + y - top_rows);
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

class NeverSettlesLongShotProfile final : public LongShotProfile {
 public:
  const char* name() const noexcept override { return "test.never-settles"; }

  bool resolve(const LongShotRequest& request,
               LongShotProfileResult& out) const override {
    if (scrolled_) return false;
    out = LongShotProfileResult{1, request.selectionRect()};
    return true;
  }

  bool scrollDown(const LongShotRequest&,
                  const LongShotProfileResult&) const override {
    scrolled_ = true;
    return true;
  }

  bool queryScrollState(const LongShotProfileResult&,
                        LongShotScrollState& out) const override {
    out.position = 0;
    out.last_position = 10;
    out.valid = true;
    return true;
  }

 private:
  mutable bool scrolled_{false};
};

}  // namespace

TEST(LongShotLimitsTest, DefaultsAreValidAndBounded) {
  const LongShotLimits limits;

  EXPECT_TRUE(limits.valid());
  EXPECT_EQ(limits.max_output_height, 30000);
  EXPECT_EQ(limits.max_frame_recaptures, 5);
  EXPECT_EQ(limits.max_scroll_settle_polls, 20);
  EXPECT_EQ(limits.max_working_bytes, 512ULL * 1024 * 1024);
  EXPECT_EQ(limits.max_duration, std::chrono::minutes(3));
  EXPECT_FALSE(limits.fixed_edges.enabled());
  EXPECT_TRUE(limits.fixed_edges.valid());
}

TEST(LongShotLimitsTest, RejectsInvalidResourceBudgets) {
  LongShotLimits limits;
  limits.max_frame_recaptures = -1;
  EXPECT_FALSE(limits.valid());
  limits.max_frame_recaptures = 0;
  limits.max_scroll_settle_polls = 1;
  EXPECT_FALSE(limits.valid());
  limits.max_scroll_settle_polls = 2;
  limits.max_working_bytes = 0;
  EXPECT_FALSE(limits.valid());
  limits.max_working_bytes = 1;
  limits.max_duration = std::chrono::milliseconds(0);
  EXPECT_FALSE(limits.valid());
  limits.max_duration = std::chrono::minutes(31);
  EXPECT_FALSE(limits.valid());
}

TEST(LongShotLimitsTest, RejectsNegativeFixedEdgeConfiguration) {
  LongShotLimits limits;
  limits.fixed_edges.top_rows = -1;
  EXPECT_FALSE(limits.valid());
  limits.fixed_edges.top_rows = 0;
  limits.fixed_edges.bottom_rows = -1;
  EXPECT_FALSE(limits.valid());
}

TEST(LongShotOutcomeTest, DefaultAndNoImageFailureHaveNoResult) {
  LongShotOutcome outcome;
  EXPECT_EQ(outcome.stop_reason, LongShotStopReason::NotStarted);
  EXPECT_EQ(outcome.quality(), LongShotResultQuality::None);
  EXPECT_FALSE(outcome.hasExportableResult());
  outcome.stop_reason = LongShotStopReason::CaptureFailed;
  EXPECT_FALSE(outcome.hasExportableResult());
  EXPECT_FALSE(outcome.isComplete());
  EXPECT_FALSE(outcome.isPartial());
}

TEST(LongShotOutcomeTest, SingleFrameRemainsOrdinaryScreenshotOnNoProgress) {
  LongShotOutcome outcome;
  outcome.image = makeStrip(20, 32, 0);
  outcome.accepted_frames = 1;
  outcome.stop_reason = LongShotStopReason::NoProgress;
  EXPECT_EQ(outcome.quality(), LongShotResultQuality::SingleFrame);
  EXPECT_TRUE(outcome.hasExportableResult());
  EXPECT_FALSE(outcome.isComplete());
  EXPECT_FALSE(outcome.isPartial());
  outcome.stop_reason = LongShotStopReason::ReachedBottom;
  EXPECT_TRUE(outcome.isComplete());
}

TEST(LongShotOutcomeTest, InterruptedCompositeIsExportableButNotComplete) {
  const LongShotStopReason reasons[] = {
      LongShotStopReason::NoProgress, LongShotStopReason::UserStopped,
      LongShotStopReason::LimitReached, LongShotStopReason::MatchFailed,
      LongShotStopReason::InputUnavailable, LongShotStopReason::TargetInvalid,
      LongShotStopReason::CaptureFailed, LongShotStopReason::StitchFailed};
  LongShotOutcome outcome;
  outcome.image = makeStrip(20, 48, 0);
  outcome.accepted_frames = 2;
  outcome.strategy = "test.scripted";
  outcome.failure_stage = LongShotFailureStage::OverlapDetection;
  outcome.diagnostic.ok = false;
  for (const auto reason : reasons) {
    SCOPED_TRACE(static_cast<int>(reason));
    outcome.stop_reason = reason;
    EXPECT_EQ(outcome.quality(), LongShotResultQuality::VerifiedComposite);
    EXPECT_TRUE(outcome.hasExportableResult());
    EXPECT_TRUE(outcome.isPartial());
    EXPECT_FALSE(outcome.isComplete());
  }
  outcome.stop_reason = LongShotStopReason::ReachedBottom;
  EXPECT_TRUE(outcome.isComplete());
  EXPECT_FALSE(outcome.isPartial());
}

TEST(LongShotOutcomeTest, CancellationNeverExportsRetainedImage) {
  LongShotOutcome outcome;
  outcome.image = makeStrip(20, 48, 0);
  outcome.accepted_frames = 2;
  for (const auto reason : {LongShotStopReason::Cancelled,
                            LongShotStopReason::NotStarted,
                            LongShotStopReason::RequestRejected}) {
    outcome.stop_reason = reason;
    EXPECT_EQ(outcome.quality(), LongShotResultQuality::VerifiedComposite);
    EXPECT_FALSE(outcome.hasExportableResult());
    EXPECT_FALSE(outcome.isComplete());
    EXPECT_FALSE(outcome.isPartial());
  }
}

TEST(LongShotOutcomeTest, MalformedPayloadCannotBecomeExportable) {
  LongShotOutcome outcome;
  outcome.stop_reason = LongShotStopReason::ReachedBottom;
  outcome.image = makeStrip(20, 32, 0);
  EXPECT_EQ(outcome.quality(), LongShotResultQuality::None);
  outcome.accepted_frames = -1;
  EXPECT_FALSE(outcome.hasExportableResult());
  outcome.accepted_frames = 2;
  outcome.image.pixels.pop_back();
  EXPECT_EQ(outcome.quality(), LongShotResultQuality::None);
  EXPECT_FALSE(outcome.isComplete());
  outcome.image.width = -20;
  EXPECT_FALSE(outcome.hasExportableResult());
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
  LongShotLimits limits;
  limits.max_output_height = 5;

  const ActionResult result = engine.captureSelection(
      request, out, {}, {}, limits);

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

  EXPECT_FALSE(worker_result.ok);
  EXPECT_TRUE(output.empty());
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
  EXPECT_EQ(capture_count, 4);
  EXPECT_EQ(profile_ptr->wheelCount(), 1);
  EXPECT_EQ(out.width, kWidth);
  EXPECT_EQ(out.height, kHeight + kScrollDelta);
}

TEST(LongShotEngineTest,
     SmoothRenderingWaitsForRepeatedPositionWithinOneInput) {
  constexpr int kWidth = 32;
  constexpr int kHeight = 40;
  auto profile = std::make_unique<ScriptedLongShotProfile>(true);
  auto* profile_ptr = profile.get();
  LongShotProfileRegistry registry;
  registry.add(std::move(profile));
  int captures = 0;
  LongShotEngine engine(
      [&](const ScreenPhysicalRect& region, Image& image) {
        const int offsets[] = {0, 5, 10, 15, 15};
        const int index = (std::min)(captures, 4);
        ++captures;
        image = makeStrip(region.width, region.height, offsets[index]);
        ActionResult result;
        result.ok = true;
        return result;
      },
      std::move(registry));
  LongShotOutcome outcome;

  const ActionResult result =
      engine.captureSelection({1, 0, 0, kWidth, kHeight}, outcome);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(outcome.stop_reason, LongShotStopReason::ReachedBottom);
  EXPECT_EQ(outcome.accepted_frames, 2);
  EXPECT_EQ(outcome.image.height, kHeight + 15);
  EXPECT_EQ(outcome.input_attempts, 1);
  EXPECT_EQ(outcome.recapture_attempts, 3);
  EXPECT_EQ(captures, 5);
  EXPECT_EQ(profile_ptr->wheelCount(), 1);
}

TEST(LongShotEngineTest, PersistentMotionStopsAtVisualSampleBudget) {
  constexpr int kWidth = 32;
  constexpr int kHeight = 40;
  auto profile = std::make_unique<ScriptedLongShotProfile>(true);
  auto* profile_ptr = profile.get();
  LongShotProfileRegistry registry;
  registry.add(std::move(profile));
  int captures = 0;
  LongShotEngine engine(
      [&](const ScreenPhysicalRect& region, Image& image) {
        const int offsets[] = {0, 2, 4, 6, 8, 10, 12};
        const int index = (std::min)(captures, 6);
        ++captures;
        image = makeStrip(region.width, region.height, offsets[index]);
        ActionResult result;
        result.ok = true;
        return result;
      },
      std::move(registry));
  LongShotOutcome outcome;

  const ActionResult result =
      engine.captureSelection({1, 0, 0, kWidth, kHeight}, outcome);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.failure_stage, "scroll_settle");
  EXPECT_EQ(outcome.accepted_frames, 1);
  EXPECT_EQ(outcome.input_attempts, 1);
  EXPECT_EQ(outcome.recapture_attempts, 5);
  EXPECT_EQ(outcome.budget_reason, LongShotBudgetReason::FrameRecaptures);
  EXPECT_EQ(captures, 7);
  EXPECT_EQ(profile_ptr->wheelCount(), 1);
}

TEST(LongShotEngineTest, ScrollSettlePollingUsesConfiguredBudget) {
  LongShotProfileRegistry registry;
  registry.add(std::make_unique<NeverSettlesLongShotProfile>());
  LongShotLimits limits;
  limits.max_scroll_settle_polls = 2;
  int captures = 0;
  LongShotEngine engine([&](const ScreenPhysicalRect& rect, Image& image) {
    ++captures;
    image = makeStrip(rect.width, rect.height, 0);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry), limits);
  LongShotOutcome outcome;

  const auto result =
      engine.captureSelection({1, 0, 0, 32, 40}, outcome);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.failure_stage, "scroll_settle");
  EXPECT_EQ(outcome.budget_reason, LongShotBudgetReason::ScrollSettlePolls);
  EXPECT_EQ(outcome.accepted_frames, 1);
  EXPECT_EQ(outcome.input_attempts, 1);
  EXPECT_EQ(captures, 1);
}

TEST(LongShotEngineTest, FrameRecaptureUsesConfiguredBudget) {
  auto profile = std::make_unique<ScriptedLongShotProfile>(true);
  auto* profile_ptr = profile.get();
  LongShotProfileRegistry registry;
  registry.add(std::move(profile));
  LongShotLimits limits;
  limits.max_frame_recaptures = 2;
  int captures = 0;
  LongShotEngine engine([&](const ScreenPhysicalRect& rect, Image& image) {
    const int offsets[] = {0, 2, 4, 6};
    image = makeStrip(rect.width, rect.height,
                      offsets[(std::min)(captures, 3)]);
    ++captures;
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry), limits);
  LongShotOutcome outcome;

  const auto result =
      engine.captureSelection({1, 0, 0, 32, 40}, outcome);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.failure_stage, "scroll_settle");
  EXPECT_EQ(outcome.budget_reason, LongShotBudgetReason::FrameRecaptures);
  EXPECT_EQ(outcome.recapture_attempts, 2);
  EXPECT_EQ(captures, 4);
  EXPECT_EQ(profile_ptr->wheelCount(), 1);
}

TEST(LongShotEngineTest, RetriesTransientLostOverlapWithoutAnotherInput) {
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
        if (capture_count == 1) {
          out = makeStrip(region.width, region.height, 0);
        } else if (capture_count == 2) {
          out = makeStrip(region.width, region.height, 100);
        } else {
          out = makeStrip(region.width, region.height, 10);
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
  EXPECT_EQ(capture_count, 4);
  EXPECT_EQ(profile_ptr->wheelCount(), 1);
  EXPECT_EQ(out.width, kWidth);
  EXPECT_EQ(out.height, kHeight + 10);
}

namespace {
class SequenceProfile final : public LongShotProfile {
 public:
  mutable int inputs{0};
  int reject_input{0};
  const char* name() const noexcept override { return "test.sequence"; }
  bool resolve(const LongShotRequest& request,
               LongShotProfileResult& out) const override {
    out = LongShotProfileResult{1, request.selectionRect()};
    return true;
  }
  bool scrollDown(const LongShotRequest&,
                  const LongShotProfileResult&) const override {
    return ++inputs != reject_input;
  }
  bool queryScrollState(const LongShotProfileResult&,
                        LongShotScrollState&) const override { return false; }
};
}

TEST(LongShotInitialPairTest, WorkingMemoryBudgetRejectsBeforeCapture) {
  LongShotProfileRegistry registry;
  registry.add(std::make_unique<SequenceProfile>());
  LongShotLimits limits;
  limits.max_working_bytes = 9000;
  int captures = 0;
  LongShotEngine engine([&](const ScreenPhysicalRect& rect, Image& image) {
    ++captures;
    image = makeStrip(rect.width, rect.height, 0);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry), limits);
  LongShotFramePair pair;

  const auto result =
      engine.captureInitialPair({1, 0, 0, 32, 40}, pair);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kResourceLimit);
  EXPECT_EQ(result.failure_stage, "safety_limit");
  EXPECT_EQ(captures, 0);
  EXPECT_FALSE(pair.valid());
}

TEST(LongShotOutcomeCaptureTest,
     AmbiguousPatternReportsReasonAndPreservesInitialFrame) {
  LongShotProfileRegistry registry;
  auto profile = std::make_unique<SequenceProfile>();
  auto* sequence = profile.get();
  registry.add(std::move(profile));
  LongShotEngine engine([](const ScreenPhysicalRect& rect, Image& image) {
    image = makePeriodicStrip(rect.width, rect.height, 5);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry));
  LongShotOutcome outcome;

  const ActionResult result =
      engine.captureSelection({1, 0, 0, 32, 40}, outcome);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.failure_stage, "overlap_detection");
  EXPECT_EQ(result.message, "longshot: overlap candidates are ambiguous");
  EXPECT_EQ(outcome.stop_reason, LongShotStopReason::MatchFailed);
  EXPECT_EQ(outcome.accepted_frames, 1);
  EXPECT_EQ(outcome.image.pixels, makePeriodicStrip(32, 40, 5).pixels);
  EXPECT_EQ(outcome.input_attempts, 1);
  EXPECT_EQ(outcome.recapture_attempts, 5);
  EXPECT_EQ(outcome.budget_reason, LongShotBudgetReason::FrameRecaptures);
  EXPECT_EQ(sequence->inputs, 1);
}

TEST(LongShotOutcomeCaptureTest,
     ExplicitFixedEdgesKeepFirstHeaderAndLatestFooter) {
  LongShotProfileRegistry registry;
  auto profile = std::make_unique<SequenceProfile>();
  auto* sequence = profile.get();
  registry.add(std::move(profile));
  LongShotLimits limits;
  limits.fixed_edges.top_rows = 2;
  limits.fixed_edges.bottom_rows = 2;
  LongShotEngine engine([&](const ScreenPhysicalRect& rect, Image& image) {
    const int frame = sequence->inputs;
    image = makeFixedEdgeFrame(
        rect.width, rect.height, 2, 2, frame * 10,
        frame == 0 ? 0xFF101010u : 0xFF202020u,
        frame == 0 ? 0xFF303030u : 0xFF404040u);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry), limits);
  LongShotOutcome outcome;
  std::vector<Image> previews;

  const ActionResult result =
      engine.captureSelection({1, 0, 0, 32, 40}, outcome,
                              [&](const Image& image) {
                                previews.push_back(image);
                              },
                              [&] { return previews.size() < 2; });

  ASSERT_TRUE(result.ok);
  EXPECT_EQ(outcome.stop_reason, LongShotStopReason::UserStopped);
  EXPECT_EQ(outcome.budget_reason, LongShotBudgetReason::None);
  EXPECT_EQ(outcome.accepted_frames, 2);
  ASSERT_EQ(outcome.image.width, 32);
  ASSERT_EQ(outcome.image.height, 50);
  ASSERT_EQ(previews.size(), 2u);
  EXPECT_EQ(previews.front().height, 40);
  EXPECT_EQ(previews.back().pixels, outcome.image.pixels);
  for (int x = 0; x < outcome.image.width; ++x) {
    EXPECT_EQ(outcome.image.pixels[x], 0xFF101010u);
    EXPECT_EQ(outcome.image.pixels[32u + x], 0xFF101010u);
  }
  for (int global_y = 0; global_y < 46; ++global_y) {
    for (int x = 0; x < outcome.image.width; ++x) {
      EXPECT_EQ(outcome.image.pixels[
                    static_cast<std::size_t>(global_y + 2) * 32u + x],
                pixelFor(x, global_y));
    }
  }
  for (int row = 48; row < 50; ++row) {
    for (int x = 0; x < outcome.image.width; ++x) {
      EXPECT_EQ(outcome.image.pixels[
                    static_cast<std::size_t>(row) * 32u + x],
                0xFF404040u);
    }
  }
}

TEST(LongShotOutcomeCaptureTest,
     OversizedFixedEdgesRequestSelectionAdjustmentAndKeepFirstFrame) {
  LongShotProfileRegistry registry;
  registry.add(std::make_unique<SequenceProfile>());
  LongShotLimits limits;
  limits.fixed_edges.top_rows = 20;
  limits.fixed_edges.bottom_rows = 20;
  LongShotEngine engine([](const ScreenPhysicalRect& rect, Image& image) {
    image = makeStrip(rect.width, rect.height, 0);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry), limits);
  LongShotOutcome outcome;

  const ActionResult result =
      engine.captureSelection({1, 0, 0, 32, 40}, outcome);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.failure_stage, "overlap_detection");
  EXPECT_EQ(result.message,
            "longshot: fixed edge exclusion is invalid; adjust selection");
  EXPECT_EQ(outcome.stop_reason, LongShotStopReason::MatchFailed);
  EXPECT_EQ(outcome.accepted_frames, 1);
  EXPECT_EQ(outcome.image.pixels, makeStrip(32, 40, 0).pixels);
}

TEST(LongShotOutcomeCaptureTest, ThirdFrameFailuresKeepExactVerifiedComposite) {
  for (int failure = 0; failure < 3; ++failure) {
    SCOPED_TRACE(failure);
    auto profile = std::make_unique<SequenceProfile>();
    auto* sequence = profile.get();
    if (failure == 2) sequence->reject_input = 2;
    LongShotProfileRegistry registry;
    registry.add(std::move(profile));
    LongShotEngine engine([&](const ScreenPhysicalRect& rect, Image& image) {
      ActionResult result;
      result.ok = !(failure == 1 && sequence->inputs >= 2);
      result.error_code = result.ok ? ErrorCode::kOk : ErrorCode::kCaptureFailed;
      image = makeStrip(rect.width, rect.height,
                        sequence->inputs == 0 ? 0
                        : sequence->inputs == 1 ? 10
                                                : 100);
      return result;
    }, std::move(registry));
    LongShotOutcome out;
    const auto result = engine.captureSelection({1, 0, 0, 32, 40}, out);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(out.accepted_frames, 2);
    EXPECT_EQ(out.image.pixels, makeStrip(32, 50, 0).pixels);
    EXPECT_EQ(out.image.height, 50);
    EXPECT_TRUE(out.isPartial());
    EXPECT_EQ(out.strategy, "test.sequence");
    EXPECT_EQ(out.stop_reason, failure == 0 ? LongShotStopReason::MatchFailed :
                               failure == 1 ? LongShotStopReason::CaptureFailed :
                                              LongShotStopReason::InputUnavailable);
    EXPECT_EQ(result.failure_frame, 3);
    EXPECT_EQ(sequence->inputs, 2);
    EXPECT_EQ(out.input_attempts, 2);
    EXPECT_EQ(out.recapture_attempts, failure == 2 ? 1 : 6);
  }
}

TEST(LongShotOutcomeCaptureTest, OutputHeightLimitDoesNotClaimReachedBottom) {
  LongShotProfileRegistry registry;
  auto profile = std::make_unique<SequenceProfile>();
  auto* sequence = profile.get();
  registry.add(std::move(profile));
  int captures = 0;
  LongShotLimits limits;
  limits.max_output_height = 45;
  LongShotEngine engine([&](const ScreenPhysicalRect& rect, Image& image) {
    ++captures;
    image = makeStrip(rect.width, rect.height, 10 * sequence->inputs);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry), limits);
  LongShotOutcome out;

  EXPECT_TRUE(engine.captureSelection({1, 0, 0, 32, 40}, out).ok);
  EXPECT_EQ(out.stop_reason, LongShotStopReason::LimitReached);
  EXPECT_EQ(out.budget_reason, LongShotBudgetReason::OutputHeight);
  EXPECT_FALSE(out.isComplete());
  EXPECT_EQ(out.accepted_frames, 1);
  EXPECT_EQ(out.image.pixels, makeStrip(32, 40, 0).pixels);
  EXPECT_EQ(out.input_attempts, 1);
  EXPECT_EQ(out.recapture_attempts, 1);
  EXPECT_EQ(captures, 3);
}

TEST(LongShotOutcomeCaptureTest, ContinuesBeyondFormerThirtyFrameLimit) {
  LongShotProfileRegistry registry;
  auto profile = std::make_unique<SequenceProfile>();
  auto* sequence = profile.get();
  registry.add(std::move(profile));
  LongShotEngine engine([&](const ScreenPhysicalRect& rect, Image& image) {
    image = makeStrip(rect.width, rect.height, sequence->inputs);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry));
  LongShotOutcome out;
  int previews = 0;

  const auto result = engine.captureSelection(
      {1, 0, 0, 32, 20}, out,
      [&](const Image&) { ++previews; },
      [&] { return previews < 31; });

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(out.stop_reason, LongShotStopReason::UserStopped);
  EXPECT_EQ(out.budget_reason, LongShotBudgetReason::None);
  EXPECT_EQ(out.accepted_frames, 31);
  EXPECT_EQ(out.input_attempts, 30);
  EXPECT_EQ(out.image.height, 50);
}

TEST(LongShotOutcomeCaptureTest, DurationBudgetKeepsInitialFrame) {
  LongShotProfileRegistry registry;
  registry.add(std::make_unique<SequenceProfile>());
  LongShotLimits limits;
  limits.max_duration = std::chrono::milliseconds(2);
  LongShotEngine engine([](const ScreenPhysicalRect& rect, Image& image) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    image = makeStrip(rect.width, rect.height, 0);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry), limits);
  LongShotOutcome out;

  const auto result = engine.captureSelection({1, 0, 0, 32, 40}, out);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(out.stop_reason, LongShotStopReason::LimitReached);
  EXPECT_EQ(out.budget_reason, LongShotBudgetReason::Duration);
  EXPECT_EQ(out.accepted_frames, 1);
  EXPECT_EQ(out.input_attempts, 0);
  EXPECT_GE(out.initial_capture_ms, 2u);
  EXPECT_GE(out.elapsed_ms, out.initial_capture_ms);
}

TEST(LongShotOutcomeCaptureTest,
     WorkingMemoryBudgetRejectsAppendBeforeAllocation) {
  LongShotProfileRegistry registry;
  auto profile = std::make_unique<SequenceProfile>();
  auto* sequence = profile.get();
  registry.add(std::move(profile));
  LongShotLimits limits;
  limits.max_working_bytes = 12000;
  LongShotEngine engine([&](const ScreenPhysicalRect& rect, Image& image) {
    image = makeStrip(rect.width, rect.height, 10 * sequence->inputs);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry), limits);
  LongShotOutcome out;

  EXPECT_TRUE(engine.captureSelection({1, 0, 0, 32, 40}, out).ok);
  EXPECT_EQ(out.stop_reason, LongShotStopReason::LimitReached);
  EXPECT_EQ(out.budget_reason, LongShotBudgetReason::WorkingMemory);
  EXPECT_EQ(out.accepted_frames, 1);
  EXPECT_EQ(out.image.pixels, makeStrip(32, 40, 0).pixels);
  EXPECT_EQ(out.input_attempts, 1);
  EXPECT_EQ(out.recapture_attempts, 1);
  EXPECT_LE(out.peak_working_bytes, limits.max_working_bytes);
}

TEST(LongShotOutcomeCaptureTest, ReportsProcessingAndPreviewMetrics) {
  LongShotProfileRegistry registry;
  auto profile = std::make_unique<SequenceProfile>();
  auto* sequence = profile.get();
  registry.add(std::move(profile));
  LongShotEngine engine([&](const ScreenPhysicalRect& rect, Image& image) {
    image = makeStrip(rect.width, rect.height, 10 * sequence->inputs);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry));
  LongShotOutcome out;
  int previews = 0;

  EXPECT_TRUE(engine.captureSelection(
      {1, 0, 0, 32, 40}, out,
      [&](const Image&) { ++previews; },
      [&] { return previews < 2; }).ok);
  EXPECT_EQ(out.preview_publications, 2);
  EXPECT_EQ(previews, 2);
  EXPECT_EQ(out.stop_reason, LongShotStopReason::UserStopped);
  EXPECT_EQ(out.budget_reason, LongShotBudgetReason::None);
  EXPECT_GE(out.peak_working_bytes, 32u * 40u * 4u);
  EXPECT_GE(out.elapsed_ms, out.initial_capture_ms);
}

TEST(LongShotOutcomeCaptureTest, StopKeepsImageButCancelDiscardsIt) {
  for (bool cancel : {false, true}) {
    LongShotProfileRegistry registry;
    registry.add(std::make_unique<SequenceProfile>());
    LongShotEngine engine([](const ScreenPhysicalRect& rect, Image& image) {
      image = makeStrip(rect.width, rect.height, 0);
      ActionResult result;
      result.ok = true;
      return result;
    }, std::move(registry));
    LongShotOutcome out;
    const auto result = engine.captureSelection({1, 0, 0, 32, 40}, out,
        [&](const Image&) { if (cancel) engine.cancel(); }, [] { return false; });
    EXPECT_EQ(out.stop_reason, cancel ? LongShotStopReason::Cancelled
                                     : LongShotStopReason::UserStopped);
    EXPECT_EQ(out.hasExportableResult(), !cancel);
    EXPECT_EQ(result.ok, !cancel);
    if (cancel) EXPECT_EQ(result.error_code, ErrorCode::kCancelled);
    EXPECT_EQ(out.accepted_frames, cancel ? 0 : 1);
    EXPECT_EQ(out.image.empty(), cancel);
  }
}

TEST(LongShotOutcomeCaptureTest, InitialCaptureFailureClearsUntrustedPayload) {
  LongShotProfileRegistry registry;
  registry.add(std::make_unique<SequenceProfile>());
  LongShotEngine engine([](const ScreenPhysicalRect& rect, Image& image) {
    image = makeStrip(rect.width, rect.height, 0);
    return ActionResult{};
  }, std::move(registry));
  LongShotOutcome out;
  EXPECT_FALSE(engine.captureSelection({1, 0, 0, 32, 40}, out).ok);
  EXPECT_EQ(out.stop_reason, LongShotStopReason::CaptureFailed);
  EXPECT_EQ(out.failure_stage, LongShotFailureStage::InitialCapture);
  EXPECT_TRUE(out.image.empty());
  EXPECT_EQ(out.accepted_frames, 0);
}

TEST(LongShotOutcomeCaptureTest, StitchAllocationFailureKeepsPreviousPixels) {
  auto budget = ImageMemoryBudget::global();
  struct RestoreLimit {
    ImageMemoryBudget budget;
    std::uint64_t limit;
    ~RestoreLimit() { budget.setLimit(limit); }
  } restore{budget, budget.snapshot().limit_bytes};
  LongShotProfileRegistry registry;
  auto profile = std::make_unique<SequenceProfile>();
  auto* sequence = profile.get();
  registry.add(std::move(profile));
  int captures = 0;
  LongShotEngine engine([&](const ScreenPhysicalRect& rect, Image& image) {
    ++captures;
    image = makeStrip(rect.width, rect.height, 10 * sequence->inputs);
    if (captures == 3) {
      EXPECT_TRUE(budget.setLimit(budget.snapshot().used_bytes));
    }
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry));
  LongShotOutcome out;
  EXPECT_FALSE(engine.captureSelection({1, 0, 0, 32, 40}, out).ok);
  EXPECT_EQ(out.stop_reason, LongShotStopReason::StitchFailed);
  EXPECT_EQ(out.failure_stage, LongShotFailureStage::Stitching);
  EXPECT_EQ(out.accepted_frames, 1);
  EXPECT_EQ(out.image.height, 40);
  for (int y = 0; y < 40; ++y)
    for (int x = 0; x < 32; ++x)
      EXPECT_EQ(out.image.pixels[y * 32 + x], pixelFor(x, y));
  EXPECT_TRUE(out.hasExportableResult());
}

TEST(LongShotOutcomeCaptureTest, IdenticalFramesReportNoProgress) {
  LongShotProfileRegistry registry;
  registry.add(std::make_unique<SequenceProfile>());
  LongShotEngine engine([](const ScreenPhysicalRect& rect, Image& image) {
    image = makeStrip(rect.width, rect.height, 0);
    ActionResult result;
    result.ok = true;
    return result;
  }, std::move(registry));
  LongShotOutcome out;
  EXPECT_TRUE(engine.captureSelection({1, 0, 0, 32, 40}, out).ok);
  EXPECT_EQ(out.stop_reason, LongShotStopReason::NoProgress);
  EXPECT_FALSE(out.isComplete());
  EXPECT_EQ(out.accepted_frames, 1);
}

}  // namespace qingying
