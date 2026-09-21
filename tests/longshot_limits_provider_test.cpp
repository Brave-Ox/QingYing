#include "qingying/app/longshot_limits_provider.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>

namespace qingying {

TEST(LongShotLimitsProviderTest, DefaultsToEngineLimits)
{
  LongShotLimitsProvider provider;

  const LongShotLimits limits = provider.snapshot();

  EXPECT_EQ(limits.max_frames, 30);
  EXPECT_EQ(limits.max_output_height, 30000);
}

TEST(LongShotLimitsProviderTest, UpdateReplacesTheNextTaskSnapshot)
{
  LongShotLimitsProvider provider;

  provider.update(LongShotLimits{45, 40000});

  const LongShotLimits limits = provider.snapshot();
  EXPECT_EQ(limits.max_frames, 45);
  EXPECT_EQ(limits.max_output_height, 40000);
}

TEST(LongShotLimitsProviderTest, ConcurrentSnapshotsNeverMixTwoUpdates)
{
  const LongShotLimits first{30, 30000};
  const LongShotLimits second{45, 40000};
  LongShotLimitsProvider provider{first};
  std::atomic<bool> keep_reading{true};
  std::atomic<bool> saw_mixed_snapshot{false};
  std::thread reader([&provider, &first, &second, &keep_reading,
                      &saw_mixed_snapshot]
  {
    while (keep_reading.load())
    {
      const LongShotLimits snapshot = provider.snapshot();
      const bool matches_first = snapshot.max_frames == first.max_frames &&
          snapshot.max_output_height == first.max_output_height;
      const bool matches_second = snapshot.max_frames == second.max_frames &&
          snapshot.max_output_height == second.max_output_height;
      if (!matches_first && !matches_second)
      {
        saw_mixed_snapshot.store(true);
        return;
      }
    }
  });

  for (int index = 0; index < 2000; ++index)
  {
    provider.update(index % 2 == 0 ? second : first);
  }
  keep_reading.store(false);
  reader.join();

  EXPECT_FALSE(saw_mixed_snapshot.load());
}

}  // namespace qingying
