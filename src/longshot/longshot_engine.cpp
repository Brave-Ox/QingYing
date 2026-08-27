#include "qingying/longshot/longshot_engine.hpp"

namespace qingying {

struct LongShotEngine::Impl {};

LongShotEngine::LongShotEngine() : impl_(new Impl) {}

LongShotEngine::~LongShotEngine() {
  delete impl_;
  impl_ = nullptr;
}

ActionResult LongShotEngine::captureForeground() {
  ActionResult r;
  r.ok = false;
  r.error_code = ErrorCode::kNotImplemented;
  r.message = "LongShotEngine::captureForeground stub";
  return r;
}

}  // namespace qingying
