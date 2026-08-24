#include "qingying/capture/capture_engine.hpp"

namespace qingying {

struct CaptureEngine::Impl {
  // DXGI Desktop Duplication / GDI fallback — to be filled in P0.
};

CaptureEngine::CaptureEngine() : impl_(new Impl) {}

CaptureEngine::~CaptureEngine() {
  delete impl_;
  impl_ = nullptr;
}

ActionResult CaptureEngine::captureRegion(int /*x*/, int /*y*/, int /*width*/,
                                          int /*height*/, Image& /*out*/) {
  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kNotImplemented;
  r.message = "CaptureEngine::captureRegion stub";
  return r;
}

ActionResult CaptureEngine::captureWindow(const std::wstring& /*query*/,
                                          Image& /*out*/) {
  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kNotImplemented;
  r.message = "CaptureEngine::captureWindow stub";
  return r;
}

ActionResult CaptureEngine::cropCenter(int /*width*/, int /*height*/,
                                       Image& /*out*/) {
  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kNotImplemented;
  r.message = "CaptureEngine::cropCenter stub";
  return r;
}

}  // namespace qingying
