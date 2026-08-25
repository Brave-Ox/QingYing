#pragma once

#include "qingying/action/types.hpp"

namespace qingying {

class LongShotEngine {
 public:
  LongShotEngine();
  ~LongShotEngine();

  LongShotEngine(const LongShotEngine&) = delete;
  LongShotEngine& operator=(const LongShotEngine&) = delete;

  ActionResult captureForeground();

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace qingying
