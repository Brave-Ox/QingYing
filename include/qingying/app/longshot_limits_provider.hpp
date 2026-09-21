#pragma once

#include "qingying/longshot/longshot_engine.hpp"

#include <mutex>

namespace qingying {

// Stores the current limits for future long-shot tasks. Callers receive a
// value snapshot, so a running worker never observes a later update.
class LongShotLimitsProvider
{
 public:
  explicit LongShotLimitsProvider(LongShotLimits limits = {});

  LongShotLimits snapshot() const;
  void update(const LongShotLimits& limits) noexcept;

 private:
  mutable std::mutex m_mutex;
  LongShotLimits m_limits;
};

}  // namespace qingying
