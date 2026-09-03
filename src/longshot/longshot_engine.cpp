#include "qingying/longshot/longshot_engine.hpp"

#include "browser_capture_receiver.hpp"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/longshot/image_stitcher.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

namespace qingying {

namespace {

constexpr auto kInitialScrollSettleDelay = std::chrono::milliseconds(150);

ActionResult makeFailure(int error_code, const char* message) {
  ActionResult result;
  result.ok = false;
  result.error_code = error_code;
  result.message = message;
  return result;
}

ActionResult makeSuccess() {
  ActionResult result;
  result.ok = true;
  result.error_code = ErrorCode::kOk;
  return result;
}

ActionResult validateRequest(const LongShotRequest& request,
                             const LongShotProfileRegistry& profiles,
                             const LongShotProfile*& profile,
                             LongShotProfileResult& profile_result) {
  profile = nullptr;
  if (!request.valid()) {
    return makeFailure(ErrorCode::kInvalidArgument,
                       "longshot: owner window and selection are required");
  }

  profile = profiles.resolve(request, profile_result);
  if (profile == nullptr) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: target application is not supported");
  }
  return makeSuccess();
}

bool sameProfileGeometry(const LongShotProfileResult& before,
                         const LongShotProfileResult& after) {
  // 某些视图在滚动后会重建子 HWND（尤其是 Explorer 的现代文件列表）。
  // profile 已经重新校验应用和选区，因此这里应保持稳定的是可见内容几何范围，
  // 而不是这个临时 HWND 的身份。
  return before.content_x == after.content_x &&
         before.content_y == after.content_y &&
         before.content_width == after.content_width &&
         before.content_height == after.content_height;
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

ActionResult captureNextFrame(CaptureEngine& capture,
                              const LongShotProfile& profile,
                              const LongShotRequest& request,
                              const LongShotProfileResult& before_profile,
                              const LongShotScrollState* before_scroll_state,
                              Image& frame,
                              LongShotProfileResult* after_profile_out,
                              LongShotScrollState* after_scroll_state_out,
                              bool* has_after_scroll_state_out,
                              bool* stable_scroll_out) {
  if (after_scroll_state_out != nullptr) {
    *after_scroll_state_out = LongShotScrollState{};
  }
  if (has_after_scroll_state_out != nullptr) {
    *has_after_scroll_state_out = false;
  }
  if (stable_scroll_out != nullptr) {
    *stable_scroll_out = false;
  }

  if (!profile.scrollDown(request, before_profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: profile did not accept wheel input");
  }
  std::this_thread::sleep_for(kInitialScrollSettleDelay);

  LongShotProfileResult after_profile;
  if (!profile.resolve(request, after_profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: target disappeared during capture");
  }
  if (!sameProfileGeometry(before_profile, after_profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: target moved or resized during capture");
  }

  LongShotScrollState after_scroll_state;
  const bool has_after_scroll_state =
      readScrollState(profile, after_profile, after_scroll_state);
  if (after_scroll_state_out != nullptr && has_after_scroll_state) {
    *after_scroll_state_out = after_scroll_state;
  }
  if (has_after_scroll_state_out != nullptr) {
    *has_after_scroll_state_out = has_after_scroll_state;
  }
  if (before_scroll_state != nullptr && has_after_scroll_state &&
      before_scroll_state->position == after_scroll_state.position) {
    // 已投递滚轮消息不代表应用一定发生了滚动。如果 profile 能观察到稳定的位置，
    // 就在捕获重复帧之前停止。
    if (stable_scroll_out != nullptr) {
      *stable_scroll_out = true;
    }
    return makeSuccess();
  }

  ActionResult result = capture.captureRegion(request.x, request.y,
                                              request.width, request.height,
                                              frame);
  if (!result.ok) {
    return result;
  }
  if (!imageMatchesRequest(frame, request)) {
    return makeFailure(ErrorCode::kCaptureFailed,
                       "longshot: next frame does not match selection");
  }
  if (after_profile_out != nullptr) {
    *after_profile_out = after_profile;
  }
  return makeSuccess();
}

}  // 匿名命名空间

struct LongShotEngine::Impl {
  Impl(CaptureEngine& capture_engine, LongShotProfileRegistry profile_registry,
       LongShotLimits capture_limits)
      : capture(&capture_engine),
        profiles(std::move(profile_registry)),
        limits(capture_limits) {}

  CaptureEngine* capture{nullptr};
  LongShotProfileRegistry profiles;
  LongShotLimits limits;
  BrowserCaptureReceiver external_result_receiver;
};

LongShotEngine::LongShotEngine(CaptureEngine& capture, LongShotLimits limits)
    : LongShotEngine(capture, LongShotProfileRegistry{}, limits) {}

LongShotEngine::LongShotEngine(CaptureEngine& capture,
                               LongShotProfileRegistry profiles,
                               LongShotLimits limits)
    : impl_(std::make_unique<Impl>(capture, std::move(profiles), limits)) {}

LongShotEngine::~LongShotEngine() = default;

bool LongShotEngine::startExternalResultReceiver(
    std::uintptr_t notification_window) {
  if (!impl_ || notification_window == 0) {
    return false;
  }
  return impl_->external_result_receiver.start(
      reinterpret_cast<HWND>(notification_window));
}

void LongShotEngine::stopExternalResultReceiver() noexcept {
  if (impl_) {
    impl_->external_result_receiver.stop();
  }
}

bool LongShotEngine::takeExternalResult(Image& image) {
  return impl_ && impl_->external_result_receiver.takeImage(image);
}

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
                       "longshot: owner window and selection are required");
  }
  if (!impl_->limits.valid()) {
    return makeFailure(ErrorCode::kInvalidArgument,
                       "longshot: safety limits are invalid");
  }
  if (request.height > impl_->limits.max_output_height) {
    return makeFailure(ErrorCode::kInvalidArgument,
                       "longshot: selection exceeds maximum output height");
  }

  const LongShotProfile* profile = nullptr;
  LongShotProfileResult current_profile;
  ActionResult result = validateRequest(request, impl_->profiles, profile,
                                        current_profile);
  if (!result.ok) {
    return result;
  }

  Image stitched;
  result = impl_->capture->captureRegion(request.x, request.y, request.width,
                                         request.height, stitched);
  if (!result.ok) {
    return result;
  }
  if (!imageMatchesRequest(stitched, request)) {
    return makeFailure(ErrorCode::kCaptureFailed,
                       "longshot: first frame does not match selection");
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

  ImageStitcher stitcher;
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

    Image next_frame;
    LongShotProfileResult after_profile;
    LongShotScrollState after_scroll_state;
    bool has_after_scroll_state = false;
    bool stable_scroll = false;
    result = captureNextFrame(*impl_->capture, *profile, request,
                              current_profile,
                              has_before_scroll_state ? &before_scroll_state
                                                      : nullptr,
                              next_frame, &after_profile, &after_scroll_state,
                              &has_after_scroll_state, &stable_scroll);
    if (!result.ok) {
      return result;
    }
    if (stable_scroll) {
      break;
    }

    int next_overlap_rows = 0;
    if (!stitcher.findOverlap(stitched, next_frame, next_overlap_rows)) {
      return makeFailure(ErrorCode::kCaptureFailed,
                         "longshot: failed to inspect next overlap");
    }
    // 整帧重叠表示本次滚动没有显示新的内容。
    if (next_overlap_rows == next_frame.height) {
      break;
    }

    const std::int64_t next_height =
        static_cast<std::int64_t>(stitched.height) +
        static_cast<std::int64_t>(next_frame.height) -
        static_cast<std::int64_t>(next_overlap_rows);
    if (next_height > impl_->limits.max_output_height) {
      break;
    }
    if (!stitcher.append(stitched, next_frame, &next_overlap_rows) ||
        stitched.width != request.width || stitched.height <= 0 ||
        stitched.pixels.empty()) {
      return makeFailure(ErrorCode::kCaptureFailed,
                         "longshot: failed to stitch next frame");
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
  result = impl_->capture->captureRegion(request.x, request.y, request.width,
                                         request.height, first_frame);
  if (!result.ok) {
    return result;
  }
  if (!imageMatchesRequest(first_frame, request)) {
    return makeFailure(ErrorCode::kCaptureFailed,
                       "longshot: first frame does not match selection");
  }

  Image second_frame;
  result = captureNextFrame(*impl_->capture, *profile, request, before_profile,
                            nullptr, second_frame, nullptr, nullptr, nullptr,
                            nullptr);
  if (!result.ok) {
    return result;
  }

  out.first_frame = std::move(first_frame);
  out.second_frame = std::move(second_frame);
  return makeSuccess();
}

}  // qingying 命名空间
