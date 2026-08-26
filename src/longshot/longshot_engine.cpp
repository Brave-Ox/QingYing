#include "qingying/longshot/longshot_engine.hpp"

#include "qingying/capture/capture_engine.hpp"
#include "qingying/longshot/image_stitcher.hpp"
#include "qingying/longshot/notepad_longshot_profile.hpp"

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <limits>
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

}  // namespace

struct LongShotEngine::Impl {
  explicit Impl(CaptureEngine& capture_engine) : capture(&capture_engine) {}

  CaptureEngine* capture{nullptr};
};

LongShotEngine::LongShotEngine(CaptureEngine& capture)
    : impl_(new Impl(capture)) {}

LongShotEngine::~LongShotEngine() {
  delete impl_;
  impl_ = nullptr;
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
  out = Image{};
  LongShotFramePair frames;
  const ActionResult capture_result = captureInitialPair(request, frames);
  if (!capture_result.ok) {
    return capture_result;
  }

  Image stitched = std::move(frames.first_frame);
  ImageStitcher stitcher;
  if (!stitcher.append(stitched, frames.second_frame) ||
      stitched.width != request.width || stitched.height <= 0 ||
      stitched.pixels.empty()) {
    return makeFailure(ErrorCode::kCaptureFailed,
                       "longshot: failed to stitch initial frames");
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

  if (!sendOneWheelDown(request, before_profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: scroll target did not accept wheel input");
  }
  std::this_thread::sleep_for(kInitialScrollSettleDelay);

  LongShotProfileResult after_profile;
  result = validateRequest(request, after_profile);
  if (!result.ok) {
    return result;
  }
  if (!sameProfileGeometry(before_profile, after_profile)) {
    return makeFailure(ErrorCode::kLongShotUnsupported,
                       "longshot: target moved or resized during capture");
  }

  Image second_frame;
  result = impl_->capture->captureRegion(request.x, request.y, request.width,
                                         request.height, second_frame);
  if (!result.ok) {
    return result;
  }
  if (!imageMatchesRequest(second_frame, request)) {
    return makeFailure(ErrorCode::kCaptureFailed,
                       "longshot: second frame does not match selection");
  }

  out.first_frame = std::move(first_frame);
  out.second_frame = std::move(second_frame);
  return makeSuccess();
}

}  // namespace qingying
