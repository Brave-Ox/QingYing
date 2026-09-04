#include "qingying/longshot/longshot_engine.hpp"

#include "qingying/capture/capture_engine.hpp"
#include "qingying/longshot/image_stitcher.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace qingying {

const char* longShotFailureStageName(LongShotFailureStage stage) noexcept {
  switch (stage) {
    case LongShotFailureStage::None:
      return "";
    case LongShotFailureStage::RequestValidation:
      return "request_validation";
    case LongShotFailureStage::SafetyLimit:
      return "safety_limit";
    case LongShotFailureStage::ProfileResolution:
      return "profile_resolution";
    case LongShotFailureStage::ScrollInput:
      return "scroll_input";
    case LongShotFailureStage::ScrollSettle:
      return "scroll_settle";
    case LongShotFailureStage::InitialCapture:
      return "initial_capture";
    case LongShotFailureStage::FrameCapture:
      return "frame_capture";
    case LongShotFailureStage::FrameValidation:
      return "frame_validation";
    case LongShotFailureStage::OverlapDetection:
      return "overlap_detection";
    case LongShotFailureStage::Stitching:
      return "stitching";
  }
  return "unknown";
}

namespace {

constexpr auto kScrollSettlePollInterval = std::chrono::milliseconds(40);
constexpr auto kScrollSettleTimeout = std::chrono::milliseconds(800);
constexpr auto kFallbackScrollSettleDelay = std::chrono::milliseconds(250);
constexpr auto kRenderSettleDelay = std::chrono::milliseconds(60);
constexpr auto kFrameRetryDelay = std::chrono::milliseconds(75);
constexpr int kFrameCaptureAttempts = 3;
constexpr int kRequiredStableSamples = 2;

ActionResult makeFailure(int error_code, const char* message,
                         LongShotFailureStage stage =
                             LongShotFailureStage::None,
                         int frame = 0) {
  ActionResult result;
  result.ok = false;
  result.error_code = error_code;
  result.message = message;
  if (stage != LongShotFailureStage::None) {
    result.failure_stage = longShotFailureStageName(stage);
    result.failure_frame = frame;
  }
  return result;
}

ActionResult makeSuccess() {
  ActionResult result;
  result.ok = true;
  result.error_code = ErrorCode::kOk;
  return result;
}

ActionResult withFailureContext(ActionResult result,
                                LongShotFailureStage stage, int frame) {
  if (!result.ok) {
    result.failure_stage = longShotFailureStageName(stage);
    result.failure_frame = frame;
  }
  return result;
}

ActionResult validateRequest(const LongShotRequest& request,
                             const LongShotProfileRegistry& profiles,
                             const LongShotProfile*& profile,
                             LongShotProfileResult& profile_result) {
  profile = nullptr;
  if (!request.valid()) {
    return makeFailure(ErrorCode::kInvalidArgument,
                       "longshot: owner window and selection are required",
                       LongShotFailureStage::RequestValidation);
  }

  profile = profiles.resolve(request, profile_result);
  if (profile == nullptr) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: target application is not supported",
                       LongShotFailureStage::ProfileResolution);
  }
  return makeSuccess();
}

bool sameProfileGeometry(const LongShotProfileResult& before,
                         const LongShotProfileResult& after) {
  // 某些视图在滚动后会重建子 HWND（尤其是 Explorer 的现代文件列表）。
  // profile 已经重新校验应用和选区，因此这里应保持稳定的是可见内容几何范围，
  // 而不是这个临时 HWND 的身份。
  return before.content.x == after.content.x &&
         before.content.y == after.content.y &&
         before.content.width == after.content.width &&
         before.content.height == after.content.height;
}

bool imageMatchesRequest(const Image& image, const LongShotRequest& request) {
  if (image.empty() || image.width != request.width ||
      image.height != request.height) {
    return false;
  }
  const std::uint64_t expected_pixels =
      static_cast<std::uint64_t>(request.width) *
      static_cast<std::uint64_t>(request.height);
  return image.pixels.size() == expected_pixels;
}

bool readScrollState(const LongShotProfile& profile,
                     const LongShotProfileResult& profile_result,
                     LongShotScrollState& out) {
  out = LongShotScrollState{};
  if (!profile.queryScrollState(profile_result, out) || !out.valid) {
    out = LongShotScrollState{};
    return false;
  }
  return true;
}

bool sameScrollState(const LongShotScrollState& before,
                     const LongShotScrollState& after) {
  return before.valid && after.valid && before.position == after.position &&
         before.last_position == after.last_position;
}

ActionResult resolveAfterScroll(const LongShotProfile& profile,
                                const LongShotRequest& request,
                                const LongShotProfileResult& before_profile,
                                LongShotProfileResult& after_profile,
                                int frame) {
  if (!profile.resolve(request, after_profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: target disappeared during capture",
                       LongShotFailureStage::ProfileResolution, frame);
  }
  if (!sameProfileGeometry(before_profile, after_profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: target moved or resized during capture",
                       LongShotFailureStage::ProfileResolution, frame);
  }
  return makeSuccess();
}

ActionResult captureFrame(const LongShotCaptureCallback& capture,
                          const LongShotRequest& request, int frame,
                          LongShotFailureStage capture_stage, Image& out) {
  out = Image{};
  if (!capture) {
    return makeFailure(ErrorCode::kNotReady,
                       "longshot: capture service is not available",
                       capture_stage, frame);
  }

  ActionResult result = capture(request.selectionRect(), out);
  if (!result.ok) {
    return withFailureContext(std::move(result), capture_stage, frame);
  }
  if (!imageMatchesRequest(out, request)) {
    return makeFailure(ErrorCode::kCaptureFailed,
                       "longshot: frame does not match selection",
                       LongShotFailureStage::FrameValidation, frame);
  }
  return makeSuccess();
}

bool retryableFrameFailure(const ActionResult& result) {
  return result.failure_stage ==
             longShotFailureStageName(LongShotFailureStage::FrameCapture) ||
         result.failure_stage ==
             longShotFailureStageName(LongShotFailureStage::FrameValidation);
}

ActionResult waitForScrollSettle(
    const LongShotProfile& profile, const LongShotRequest& request,
    const LongShotProfileResult& before_profile,
    const LongShotScrollState& before_scroll_state,
    const LongShotContinueCallback& should_continue,
    LongShotProfileResult& after_profile,
    LongShotScrollState& after_scroll_state, bool& has_after_scroll_state,
    bool& no_movement, bool& stopped, int frame) {
  after_profile = before_profile;
  after_scroll_state = before_scroll_state;
  has_after_scroll_state = false;
  no_movement = false;
  stopped = false;

  const auto deadline = std::chrono::steady_clock::now() +
                        kScrollSettleTimeout;
  LongShotProfileResult latest_profile = before_profile;
  LongShotScrollState latest_state = before_scroll_state;
  LongShotScrollState previous_state = before_scroll_state;
  bool resolved_any = false;
  bool saw_movement = false;
  int stable_samples = 0;

  while (std::chrono::steady_clock::now() < deadline) {
    if (should_continue && !should_continue()) {
      stopped = true;
      return makeSuccess();
    }

    LongShotProfileResult candidate_profile;
    if (!profile.resolve(request, candidate_profile)) {
      std::this_thread::sleep_for(kScrollSettlePollInterval);
      continue;
    }
    resolved_any = true;
    if (!sameProfileGeometry(before_profile, candidate_profile)) {
      return makeFailure(ErrorCode::kLongShotUnsupported,
                         "longshot: target moved or resized during capture",
                         LongShotFailureStage::ProfileResolution, frame);
    }

    LongShotScrollState candidate_state;
    if (!readScrollState(profile, candidate_profile, candidate_state)) {
      // A profile may advertise a native state while the target is being
      // recreated. Fall back to a conservative render delay and capture the
      // fixed selection instead of sending another wheel message.
      std::this_thread::sleep_for(kFallbackScrollSettleDelay);
      if (should_continue && !should_continue()) {
        stopped = true;
        return makeSuccess();
      }
      after_profile = candidate_profile;
      return makeSuccess();
    }

    latest_profile = candidate_profile;
    latest_state = candidate_state;
    has_after_scroll_state = true;

    if (!sameScrollState(before_scroll_state, candidate_state)) {
      saw_movement = true;
      if (sameScrollState(previous_state, candidate_state)) {
        ++stable_samples;
      } else {
        stable_samples = 1;
      }
    } else if (saw_movement) {
      // The target moved and then returned to its old state. Do not treat
      // that as settled until the new state is observed twice in a row.
      saw_movement = false;
      stable_samples = 0;
    }
    previous_state = candidate_state;

    if (saw_movement && stable_samples >= kRequiredStableSamples) {
      std::this_thread::sleep_for(kRenderSettleDelay);
      after_profile = latest_profile;
      after_scroll_state = latest_state;
      return makeSuccess();
    }

    std::this_thread::sleep_for(kScrollSettlePollInterval);
  }

  if (should_continue && !should_continue()) {
    stopped = true;
    return makeSuccess();
  }
  if (!resolved_any) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: target did not settle after scrolling",
                       LongShotFailureStage::ScrollSettle, frame);
  }

  after_profile = latest_profile;
  after_scroll_state = latest_state;
  if (!saw_movement) {
    // A wheel message can be accepted while the target is already at the
    // end, or while the application ignores it. This is a normal terminal
    // condition, not a stitching failure.
    no_movement = true;
    return makeSuccess();
  }

  // The position changed but never reported two identical samples before the
  // deadline. Capture the latest frame once; the render delay above is not
  // available in this timeout path, so add the same guard here.
  std::this_thread::sleep_for(kRenderSettleDelay);
  return makeSuccess();
}

ActionResult captureNextFrame(
    const LongShotCaptureCallback& capture_frame,
    const LongShotProfile& profile, const LongShotRequest& request,
    const LongShotProfileResult& before_profile,
    const LongShotScrollState* before_scroll_state, int frame_number,
    const LongShotContinueCallback& should_continue, Image& frame,
    LongShotProfileResult* after_profile_out,
    LongShotScrollState* after_scroll_state_out,
    bool* has_after_scroll_state_out, bool* stable_scroll_out,
    bool* stopped_out) {
  if (after_scroll_state_out != nullptr) {
    *after_scroll_state_out = LongShotScrollState{};
  }
  if (has_after_scroll_state_out != nullptr) {
    *has_after_scroll_state_out = false;
  }
  if (stable_scroll_out != nullptr) {
    *stable_scroll_out = false;
  }
  if (stopped_out != nullptr) {
    *stopped_out = false;
  }

  if (!profile.scrollDown(request, before_profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: profile did not accept wheel input",
                       LongShotFailureStage::ScrollInput, frame_number);
  }

  LongShotProfileResult after_profile;
  LongShotScrollState after_scroll_state;
  bool has_after_scroll_state = false;
  bool no_movement = false;
  bool stopped = false;

  ActionResult result;
  if (before_scroll_state != nullptr) {
    result = waitForScrollSettle(
        profile, request, before_profile, *before_scroll_state, should_continue,
        after_profile, after_scroll_state, has_after_scroll_state, no_movement,
        stopped, frame_number);
  } else {
    if (should_continue && !should_continue()) {
      stopped = true;
      result = makeSuccess();
    } else {
      std::this_thread::sleep_for(kFallbackScrollSettleDelay);
      result = resolveAfterScroll(profile, request, before_profile,
                                  after_profile, frame_number);
      if (result.ok) {
        has_after_scroll_state =
            readScrollState(profile, after_profile, after_scroll_state);
      }
    }
  }
  if (!result.ok) {
    return result;
  }

  if (after_profile_out != nullptr && after_profile.valid()) {
    *after_profile_out = after_profile;
  }
  if (after_scroll_state_out != nullptr && has_after_scroll_state) {
    *after_scroll_state_out = after_scroll_state;
  }
  if (has_after_scroll_state_out != nullptr) {
    *has_after_scroll_state_out = has_after_scroll_state;
  }
  if (stopped) {
    if (stopped_out != nullptr) {
      *stopped_out = true;
    }
    return makeSuccess();
  }
  if (no_movement) {
    if (stable_scroll_out != nullptr) {
      *stable_scroll_out = true;
    }
    return makeSuccess();
  }

  return captureFrame(capture_frame, request, frame_number,
                      LongShotFailureStage::FrameCapture, frame);
}

}  // 匿名命名空间

struct LongShotEngine::Impl {
  Impl(LongShotCaptureCallback capture_callback,
       LongShotProfileRegistry profile_registry,
       LongShotLimits capture_limits)
      : capture(std::move(capture_callback)),
        profiles(std::move(profile_registry)),
        limits(capture_limits) {}

  LongShotCaptureCallback capture;
  LongShotProfileRegistry profiles;
  LongShotLimits limits;
};

LongShotEngine::LongShotEngine(CaptureEngine& capture, LongShotLimits limits)
    : LongShotEngine(
          LongShotCaptureCallback{
              [&capture](const ScreenPhysicalRect& region, Image& out) {
                return capture.captureRegion(region, out);
              }},
          LongShotProfileRegistry{}, limits) {}

LongShotEngine::LongShotEngine(CaptureEngine& capture,
                               LongShotProfileRegistry profiles,
                               LongShotLimits limits)
    : LongShotEngine(
          LongShotCaptureCallback{
              [&capture](const ScreenPhysicalRect& region, Image& out) {
                return capture.captureRegion(region, out);
              }},
          std::move(profiles), limits) {}

LongShotEngine::LongShotEngine(LongShotCaptureCallback capture,
                               LongShotProfileRegistry profiles,
                               LongShotLimits limits)
    : impl_(std::make_unique<Impl>(std::move(capture), std::move(profiles),
                                   limits)) {}

LongShotEngine::~LongShotEngine() = default;

bool LongShotFramePair::valid() const {
  return !first_frame.empty() && !second_frame.empty() &&
         first_frame.width == second_frame.width &&
         first_frame.height == second_frame.height;
}

void LongShotFramePair::clear() {
  first_frame = Image{};
  second_frame = Image{};
}

ActionResult LongShotEngine::captureSelection(const LongShotRequest& request,
                                              Image& out) {
  return captureSelection(request, out, {}, {});
}

ActionResult LongShotEngine::captureSelection(
    const LongShotRequest& request, Image& out,
    LongShotProgressCallback on_progress,
    LongShotContinueCallback should_continue) {
  out = Image{};
  if (!request.valid()) {
    return makeFailure(ErrorCode::kInvalidArgument,
                       "longshot: owner window and selection are required",
                       LongShotFailureStage::RequestValidation);
  }
  if (!impl_->limits.valid()) {
    return makeFailure(ErrorCode::kInvalidArgument,
                       "longshot: safety limits are invalid",
                       LongShotFailureStage::SafetyLimit);
  }
  if (request.height > impl_->limits.max_output_height) {
    return makeFailure(ErrorCode::kInvalidArgument,
                       "longshot: selection exceeds maximum output height",
                       LongShotFailureStage::SafetyLimit, 1);
  }

  const LongShotProfile* profile = nullptr;
  LongShotProfileResult current_profile;
  ActionResult result = validateRequest(request, impl_->profiles, profile,
                                        current_profile);
  if (!result.ok) {
    return result;
  }

  Image stitched;
  result = captureFrame(impl_->capture, request, 1,
                        LongShotFailureStage::InitialCapture, stitched);
  if (!result.ok) {
    return result;
  }

  if (on_progress) {
    on_progress(stitched);
  }

  // 用户主动停止属于正常完成：已经累计的帧构成有效长截图结果，仍可继续复制、
  // 保存或 Pin。
  if (should_continue && !should_continue()) {
    out = std::move(stitched);
    return makeSuccess();
  }

  LongShotScrollState current_scroll_state;
  if (readScrollState(*profile, current_profile, current_scroll_state) &&
      current_scroll_state.atBottom()) {
    out = std::move(stitched);
    return makeSuccess();
  }

  ImageStitchOptions stitch_options;
  stitch_options.min_overlap_rows = 16;
  stitch_options.right_edge_exclusion_pixels = 12;
  // Text anti-aliasing, caret blinking and other small repaint differences
  // are expected after a real scroll. Keep the overlap requirement enabled,
  // but allow a small per-channel difference and sparse dynamic pixels.
  stitch_options.channel_tolerance = 2;
  stitch_options.minimum_match_per_mille = 960;
  stitch_options.require_overlap = true;
  ImageStitcher stitcher(stitch_options);
  int frame_count = 1;
  while (frame_count < impl_->limits.max_frames &&
         stitched.height < impl_->limits.max_output_height) {
    if (should_continue && !should_continue()) {
      break;
    }

    LongShotScrollState before_scroll_state;
    const bool has_before_scroll_state =
        readScrollState(*profile, current_profile, before_scroll_state);
    if (has_before_scroll_state && before_scroll_state.atBottom()) {
      break;
    }

    const int next_frame_number = frame_count + 1;
    Image next_frame;
    LongShotProfileResult after_profile;
    LongShotScrollState after_scroll_state;
    bool has_after_scroll_state = false;
    bool stable_scroll = false;
    bool stopped = false;
    result = captureNextFrame(impl_->capture, *profile, request,
                              current_profile,
                              has_before_scroll_state ? &before_scroll_state
                                                      : nullptr,
                              next_frame_number, should_continue,
                              next_frame, &after_profile, &after_scroll_state,
                              &has_after_scroll_state, &stable_scroll,
                              &stopped);
    if (stopped) {
      break;
    }
    if (!result.ok && !retryableFrameFailure(result)) {
      return result;
    }
    if (stable_scroll) {
      break;
    }

    Image candidate_frame = std::move(next_frame);
    ActionResult last_frame_failure = result;
    int next_overlap_rows = 0;
    bool matched_overlap = false;
    bool full_overlap = false;
    bool interrupted = false;
    for (int attempt = 0; attempt < kFrameCaptureAttempts; ++attempt) {
      if (attempt > 0) {
        if (should_continue && !should_continue()) {
          interrupted = true;
          break;
        }
        std::this_thread::sleep_for(kFrameRetryDelay);
        result = captureFrame(impl_->capture, request, next_frame_number,
                              LongShotFailureStage::FrameCapture,
                              candidate_frame);
      }

      if (!result.ok) {
        last_frame_failure = result;
        continue;
      }

      int candidate_overlap = 0;
      if (!stitcher.findOverlap(stitched, candidate_frame,
                                candidate_overlap)) {
        last_frame_failure = makeFailure(
            ErrorCode::kCaptureFailed,
            "longshot: failed to inspect next overlap",
            LongShotFailureStage::OverlapDetection, next_frame_number);
        continue;
      }

      // A stale frame can still be fully identical immediately after the
      // scroll message. Re-capture at the same position before treating it
      // as the terminal no-new-content condition.
      if (candidate_overlap == candidate_frame.height) {
        if (attempt + 1 == kFrameCaptureAttempts) {
          full_overlap = true;
        }
        continue;
      }
      if (candidate_overlap == 0) {
        last_frame_failure = makeFailure(
            ErrorCode::kCaptureFailed,
            "longshot: no reliable overlap was found",
            LongShotFailureStage::OverlapDetection, next_frame_number);
        continue;
      }

      next_overlap_rows = candidate_overlap;
      matched_overlap = true;
      break;
    }

    if (interrupted) {
      break;
    }
    if (full_overlap) {
      // 整帧重叠表示本次滚动没有显示新的内容。
      break;
    }
    if (!matched_overlap) {
      return last_frame_failure.ok
                 ? makeFailure(ErrorCode::kCaptureFailed,
                               "longshot: no reliable overlap was found",
                               LongShotFailureStage::OverlapDetection,
                               next_frame_number)
                 : last_frame_failure;
    }

    const std::int64_t next_height =
        static_cast<std::int64_t>(stitched.height) +
        static_cast<std::int64_t>(candidate_frame.height) -
        static_cast<std::int64_t>(next_overlap_rows);
    if (next_height > impl_->limits.max_output_height) {
      break;
    }
    if (!stitcher.append(stitched, candidate_frame, &next_overlap_rows) ||
        stitched.width != request.width || stitched.height <= 0 ||
        stitched.pixels.empty()) {
      return makeFailure(ErrorCode::kCaptureFailed,
                         "longshot: failed to stitch next frame",
                         LongShotFailureStage::Stitching, next_frame_number);
    }

    ++frame_count;
    if (on_progress) {
      on_progress(stitched);
    }

    current_profile = after_profile;
    if (has_after_scroll_state && after_scroll_state.atBottom()) {
      break;
    }
  }

  out = std::move(stitched);
  return makeSuccess();
}

ActionResult LongShotEngine::captureInitialPair(const LongShotRequest& request,
                                                LongShotFramePair& out) {
  out.clear();

  const LongShotProfile* profile = nullptr;
  LongShotProfileResult before_profile;
  ActionResult result = validateRequest(request, impl_->profiles, profile,
                                        before_profile);
  if (!result.ok) {
    return result;
  }

  Image first_frame;
  result = captureFrame(impl_->capture, request, 1,
                        LongShotFailureStage::InitialCapture, first_frame);
  if (!result.ok) {
    return result;
  }

  Image second_frame;
  result = captureNextFrame(impl_->capture, *profile, request, before_profile,
                            nullptr, 2, {}, second_frame, nullptr, nullptr,
                            nullptr, nullptr, nullptr);
  if (!result.ok) {
    return result;
  }

  out.first_frame = std::move(first_frame);
  out.second_frame = std::move(second_frame);
  return makeSuccess();
}

}  // qingying 命名空间
