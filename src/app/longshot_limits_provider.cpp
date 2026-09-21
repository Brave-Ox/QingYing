#include "qingying/app/longshot_limits_provider.hpp"

#include <mutex>

namespace qingying {

LongShotLimitsProvider::LongShotLimitsProvider(LongShotLimits limits)
    : m_limits(limits)
{
}

LongShotLimits LongShotLimitsProvider::snapshot() const
{
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_limits;
}

void LongShotLimitsProvider::update(const LongShotLimits& limits) noexcept
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_limits = limits;
}

}  // namespace qingying
