#include "qingying/longshot/longshot_engine.hpp"

#include "qingying/capture/capture_engine.hpp"

namespace qingying {

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

ActionResult LongShotEngine::captureSelection(const LongShotRequest& request,
                                              Image& out) {
  out = Image{};
  if (!request.valid()) {
    ActionResult r;
    r.ok = false;
    r.error_code = ErrorCode::kInvalidArgument;
    r.message = "longshot: owner window and selection are required";
    return r;
  }

  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kNotImplemented;
  r.message = "LongShotEngine::captureSelection stub";
  return r;
}

}  // namespace qingying
