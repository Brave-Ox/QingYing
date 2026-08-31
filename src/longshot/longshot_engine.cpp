#include "qingying/longshot/longshot_engine.hpp"

#include "qingying/capture/capture_engine.hpp"
#include "qingying/longshot/image_stitcher.hpp"
#include "qingying/longshot/notepad_longshot_profile.hpp"

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <utility>

namespace qingying {

namespace {

constexpr auto kInitialScrollSettleDelay = std::chrono::milliseconds(150);
constexpr UINT kScrollDispatchTimeoutMs = 500;

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
                             LongShotProfileResult& profile) {
  if (!request.valid()) {
    return makeFailure(ErrorCode::kInvalidArgument,
                       "longshot: owner window and selection are required");
  }
  if (!resolveNotepadProfile(request.owner_window, profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: target is not a supported Notepad window");
  }
  if (!profile.containsSelection(request.x, request.y, request.width,
                                 request.height)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: selection must stay inside Notepad content");
  }
  return makeSuccess();
}

bool sameProfileGeometry(const LongShotProfileResult& before,
                         const LongShotProfileResult& after) {
  return before.scroll_target == after.scroll_target &&
         before.content_x == after.content_x &&
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

bool sendOneWheelDown(const LongShotRequest& request,
                      const LongShotProfileResult& profile) {
  const HWND scroll_target = reinterpret_cast<HWND>(profile.scroll_target);
  if (scroll_target == nullptr || !IsWindow(scroll_target)) {
    return false;
  }

  const std::int64_t center_x =
      static_cast<std::int64_t>(request.x) + request.width / 2;
  const std::int64_t center_y =
      static_cast<std::int64_t>(request.y) + request.height / 2;
  if (center_x < (std::numeric_limits<int>::min)() ||
      center_x > (std::numeric_limits<int>::max)() ||
      center_y < (std::numeric_limits<int>::min)() ||
      center_y > (std::numeric_limits<int>::max)()) {
    return false;
  }

  const WORD wheel_delta = static_cast<WORD>(static_cast<SHORT>(-WHEEL_DELTA));
  const WPARAM wheel_parameters = MAKEWPARAM(0, wheel_delta);
  const LPARAM screen_point =
      MAKELPARAM(static_cast<WORD>(static_cast<int>(center_x)),
                 static_cast<WORD>(static_cast<int>(center_y)));
  DWORD_PTR message_result = 0;
  return SendMessageTimeoutW(scroll_target, WM_MOUSEWHEEL, wheel_parameters,
                             screen_point, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                             kScrollDispatchTimeoutMs, &message_result) != 0;
}

ActionResult captureNextFrame(CaptureEngine& capture,
                              const LongShotRequest& request,
                              const LongShotProfileResult& before_profile,
                              Image& frame,
                              LongShotProfileResult* after_profile_out) {
  if (!sendOneWheelDown(request, before_profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: scroll target did not accept wheel input");
  }
  std::this_thread::sleep_for(kInitialScrollSettleDelay);

  LongShotProfileResult after_profile;
  ActionResult result = validateRequest(request, after_profile);
  if (!result.ok) {
    return result;
  }
  if (!sameProfileGeometry(before_profile, after_profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: target moved or resized during capture");
  }

  result = capture.captureRegion(request.x, request.y, request.width,
                                 request.height, frame);
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

}  // namespace

struct LongShotEngine::Impl {
  explicit Impl(CaptureEngine& capture_engine, LongShotLimits capture_limits)
      : capture(&capture_engine), limits(capture_limits) {}

  CaptureEngine* capture{nullptr};
  LongShotLimits limits;
};

LongShotEngine::LongShotEngine(CaptureEngine& capture, LongShotLimits limits)
    : impl_(std::make_unique<Impl>(capture, limits)) {}

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

  LongShotProfileResult current_profile;
  ActionResult result = validateRequest(request, current_profile);
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

  // A user stop is a clean completion: the frames already accumulated are a
  // valid long-shot result and should remain available for copy/save/pin.
  if (should_continue && !should_continue()) {
    out = std::move(stitched);
    return makeSuccess();
  }

  bool at_bottom = false;
  if (queryNotepadScrollAtBottom(current_profile, at_bottom) && at_bottom) {
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
    Image next_frame;
    LongShotProfileResult after_profile;
    result = captureNextFrame(*impl_->capture, request, current_profile,
                              next_frame, &after_profile);
    if (!result.ok) {
      return result;
    }

    int next_overlap_rows = 0;
    if (!stitcher.findOverlap(stitched, next_frame, next_overlap_rows)) {
      return makeFailure(ErrorCode::kCaptureFailed,
                         "longshot: failed to inspect next overlap");
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
    if (next_overlap_rows == next_frame.height) {
      break;
    }

    current_profile = after_profile;
    if (queryNotepadScrollAtBottom(current_profile, at_bottom) &&
        at_bottom) {
      break;
    }
  }

  out = std::move(stitched);
  return makeSuccess();
}

ActionResult LongShotEngine::captureInitialPair(const LongShotRequest& request,
                                                LongShotFramePair& out) {
  out.clear();

  LongShotProfileResult before_profile;
  ActionResult result = validateRequest(request, before_profile);
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
  result = captureNextFrame(*impl_->capture, request, before_profile,
                            second_frame, nullptr);
  if (!result.ok) {
    return result;
  }

  out.first_frame = std::move(first_frame);
  out.second_frame = std::move(second_frame);
  return makeSuccess();
}

}  // namespace qingying
