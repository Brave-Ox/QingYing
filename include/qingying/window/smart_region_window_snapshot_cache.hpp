#pragma once

#include "qingying/window/smart_region_types.hpp"

#include <cstddef>
#include <cstdint>

namespace qingying {

// A short-lived cache for root window geometry on the overlay hover path.
// The detector validates the window again before trusting a cache hit.
struct SmartRegionWindowSnapshotCacheStats {
  std::uint64_t hits{0};
  std::uint64_t misses{0};
  std::uint64_t stores{0};
  std::uint64_t invalidations{0};
};

class SmartRegionWindowSnapshotCache final {
 public:
  using ClockFunction = std::uint64_t (*)(void*) noexcept;

  explicit SmartRegionWindowSnapshotCache(ClockFunction clock = nullptr,
                                          void* context = nullptr) noexcept
      : m_clock(clock), m_clock_context(context) {}

  bool lookup(int screen_x, int screen_y, SmartRegionWindowSnapshot& out,
              std::uint64_t* age_ms = nullptr) noexcept;
  void store(const SmartRegionWindowSnapshot& snapshot) noexcept;
  void clear() noexcept;

  SmartRegionWindowSnapshotCacheStats stats() const noexcept;
  static constexpr std::uint64_t lifetimeMs() noexcept { return 48; }
  std::size_t storageBytes() const noexcept { return sizeof(*this); }

 private:
  std::uint64_t nowMs() const noexcept;

  ClockFunction m_clock{nullptr};
  void* m_clock_context{nullptr};
  SmartRegionWindowSnapshot m_snapshot;
  SmartRegionWindowSnapshotCacheStats m_stats;
  std::uint64_t m_cached_at_ms{0};
  bool m_valid{false};
};

}  // namespace qingying
