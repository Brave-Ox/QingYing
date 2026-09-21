#include "qingying/app/result_store.h"

#include <gtest/gtest.h>

#include <utility>
#include <thread>
#include <limits>

namespace qingying {
namespace {

Image makeImage(std::uint32_t pixel) {
  Image image;
  image.width = 2;
  image.height = 1;
  image.pixels = {pixel, pixel};
  return image;
}

}  // namespace

TEST(ResultStoreTest, EmptyByDefault) {
  ResultStore store;

  EXPECT_EQ(store.currentId(), kInvalidResultId);
  EXPECT_FALSE(store.current().has_value());
  EXPECT_FALSE(store.get(1).has_value());
}

TEST(ResultStoreTest, PublishReplacesPreviousResult) {
  ResultStore store;

  const ResultId first_id = store.publish(makeImage(0xFF112233u));
  const ResultId second_id = store.publish(makeImage(0xFF445566u));

  ASSERT_NE(first_id, kInvalidResultId);
  ASSERT_NE(second_id, kInvalidResultId);
  EXPECT_NE(first_id, second_id);
  EXPECT_EQ(store.currentId(), second_id);

  EXPECT_FALSE(store.get(first_id).has_value());

  const auto current = store.current();
  ASSERT_TRUE(current.has_value());
  EXPECT_EQ(current->result_id, second_id);
  EXPECT_EQ(current->image.pixels[0], 0xFF445566u);
}

TEST(ResultStoreTest, ResolvesCurrentAndExplicitResultSelections) {
  ResultStore store;
  const ResultId first_id = store.publish(makeImage(0xFF112233u));
  const ResultId second_id = store.publish(makeImage(0xFF445566u));

  EXPECT_EQ(store.resolve(ResultSelection::current()), second_id);
  EXPECT_EQ(store.resolve(ResultSelection::specific(first_id)), first_id);
  EXPECT_FALSE(store.get(first_id).has_value());
  EXPECT_EQ(store.resolve(ResultSelection{
                ResultSelectionKind::Explicit, kInvalidResultId}),
            kInvalidResultId);
}

TEST(ResultStoreTest, InvalidPublishDoesNotReplaceCurrentResult) {
  ResultStore store;
  const ResultId current_id = store.publish(makeImage(0xFF112233u));

  Image invalid;
  invalid.width = 2;
  invalid.height = 1;
  invalid.pixels = {0xFFFFFFFFu};

  EXPECT_EQ(store.publish(std::move(invalid)), kInvalidResultId);
  EXPECT_EQ(store.currentId(), current_id);
  ASSERT_TRUE(store.current().has_value());
  EXPECT_EQ(store.current()->image.pixels[0], 0xFF112233u);
}

TEST(ResultStoreTest, ClearDropsAllPublishedResults) {
  ResultStore store;
  const ResultId result_id = store.publish(makeImage(0xFF112233u));

  store.clear();

  EXPECT_EQ(store.currentId(), kInvalidResultId);
  EXPECT_FALSE(store.current().has_value());
  EXPECT_FALSE(store.get(result_id).has_value());
}

TEST(ResultStoreTest, ReleaseDropsTheSelectedResult) {
  ResultStore store;
  const ResultId result_id = store.publish(makeImage(0xFF112233u));

  store.release(result_id);

  EXPECT_EQ(store.currentId(), kInvalidResultId);
  EXPECT_FALSE(store.current().has_value());
  EXPECT_FALSE(store.get(result_id).has_value());
}

}  // namespace qingying

namespace qingying {
namespace {
class ResultRetentionTest : public ::testing::Test {
 protected:
  using Clock = std::chrono::steady_clock;
  Clock::time_point now{};
  AutomationLimits limits = [] {
    AutomationLimits value;
    value.max_capture_pixels = 4;
    value.max_result_bytes = 32;
    value.max_retained_result_bytes = 48;
    value.max_tombstones_per_connection = 2;
    return value;
  }();
  ResultStore store{limits, [this] { return now; }};
  Image pixels(int count = 4) { return Image{count, 1, std::vector<std::uint32_t>(count, 42)}; }
};
}  // namespace

TEST_F(ResultRetentionTest, DeadlineDoesNotRenewAndGuiDoesNotExpire) {
  const auto gui = store.publish(pixels());
  const auto id = store.publish(2, pixels());
  ASSERT_NE(id, kInvalidResultId);
  const auto lease = store.acquire(2, id);
  ASSERT_TRUE(lease);
  EXPECT_EQ(store.expiresIn(2, id), std::chrono::milliseconds{60000});
  now += std::chrono::milliseconds{1234};
  EXPECT_EQ(store.expiresIn(2, id), std::chrono::milliseconds{58766});
  EXPECT_EQ(store.acquire(2, id).metadata().expires_at, lease.metadata().expires_at);
  now += std::chrono::milliseconds{58766};
  EXPECT_FALSE(store.acquire(2, id));
  EXPECT_FALSE(store.acquire(2, ResultSelection::current()));
  EXPECT_EQ(store.currentId(2), kInvalidResultId);
  EXPECT_EQ(store.resultStatus(2, id), ErrorCode::kResultExpired);
  EXPECT_EQ(store.resultStatus(3, id), ErrorCode::kResultNotFound);
  store.sweep();
  EXPECT_EQ(store.resultStatus(2, id), ErrorCode::kResultExpired);
  EXPECT_EQ(lease.image()->pixels[0], 42u);
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, 16u);
  EXPECT_TRUE(store.acquire(kGuiResultScopeId, gui));
  EXPECT_FALSE(store.expiresIn(kGuiResultScopeId, gui));
}

TEST_F(ResultRetentionTest, SweepReclaimsOnlyUnleasedPixels) {
  store.publish(2, pixels());
  const auto b = store.publish(3, pixels());
  auto held = store.acquire(3, b);
  now += std::chrono::seconds{60};
  store.sweep();
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, 16u);
  held = {};
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, 0u);
}

TEST_F(ResultRetentionTest, ReplacementCountsOldLeaseAndDoesNotEvictOthers) {
  const auto a = store.publish(2, pixels());
  auto old = store.acquire(2, a);
  auto duplicate = old;
  const auto b = store.publish(3, pixels());
  const auto next = store.publish(2, pixels());
  ASSERT_NE(next, kInvalidResultId);
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, 48u);
  EXPECT_FALSE(store.acquire(2, a));
  EXPECT_EQ(store.publish(4, pixels(1)), kInvalidResultId);
  EXPECT_TRUE(store.acquire(3, b));
  EXPECT_TRUE(store.acquire(2, next));
  old = {};
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, 48u);
  duplicate = {};
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, 32u);
}

TEST_F(ResultRetentionTest, DisconnectAndReleaseKeepChargesUntilLastLease) {
  const auto id = store.publish(2, pixels());
  auto held = store.acquire(2, id);
  EXPECT_EQ(store.releaseResult(3, id), ErrorCode::kResultNotFound);
  EXPECT_EQ(store.releaseResult(2, id), ErrorCode::kOk);
  EXPECT_EQ(store.releaseResult(2, id), ErrorCode::kOk);
  EXPECT_FALSE(store.acquire(2, id));
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, 16u);
  store.clearScope(2);
  EXPECT_EQ(store.releaseResult(2, id), ErrorCode::kResultNotFound);
  EXPECT_EQ(held.image()->pixels[0], 42u);
  held = {};
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, 0u);
}

TEST_F(ResultRetentionTest, ReservationTransfersWithoutDoubleCharging) {
  auto reservation = store.reserve(2, 4, 1);
  ASSERT_TRUE(reservation);
  EXPECT_EQ(store.budgetSnapshot().external_reserved_bytes, 16u);
  const auto id = store.publish(2, pixels(), std::move(reservation));
  ASSERT_NE(id, kInvalidResultId);
  EXPECT_FALSE(reservation);
  EXPECT_EQ(store.budgetSnapshot().external_reserved_bytes, 0u);
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, 16u);
  EXPECT_EQ(store.publish(2, pixels(), std::move(reservation)), kInvalidResultId);
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, 16u);
}

TEST_F(ResultRetentionTest, PreflightReservationDoesNotReplaceScopeUntilPublish) {
  const auto old = store.publish(2, pixels(1));
  ASSERT_NE(old, kInvalidResultId);
  auto reservation = store.reserve(2, 4, 1);
  ASSERT_TRUE(reservation);
  EXPECT_TRUE(store.acquire(2, old));
  EXPECT_EQ(store.budgetSnapshot().external_reserved_bytes, 16u);
  store.clearScope(2);
  EXPECT_FALSE(store.acquire(2, old));
  const auto replacement =
      store.publish(2, pixels(), std::move(reservation), {-5, 7, 4, 1});
  ASSERT_NE(replacement, kInvalidResultId);
  EXPECT_EQ(store.acquire(2, replacement).metadata().bounds,
            (ScreenPhysicalRect{-5, 7, 4, 1}));
}

TEST_F(ResultRetentionTest, WrongScopeSizeStoreOrCapacityRollsBackReservation) {
  auto reservation = store.reserve(2, 4, 1);
  EXPECT_EQ(store.publish(3, pixels(), std::move(reservation)), kInvalidResultId);
  reservation = store.reserve(2, 4, 1);
  EXPECT_EQ(store.publish(2, pixels(3), std::move(reservation)), kInvalidResultId);
  ResultStore other(limits);
  reservation = other.reserve(2, 4, 1);
  EXPECT_EQ(store.publish(2, pixels(), std::move(reservation)), kInvalidResultId);
  EXPECT_EQ(other.budgetSnapshot().reserved_bytes, 0u);
  Image padded = pixels();
  padded.pixels.reserve(20);
  reservation = store.reserve(2, 4, 1);
  EXPECT_EQ(store.publish(2, std::move(padded), std::move(reservation)), kInvalidResultId);
  EXPECT_EQ(store.budgetSnapshot().reserved_bytes, 0u);
  EXPECT_EQ(store.budgetSnapshot().retained_bytes, 0u);
}

TEST_F(ResultRetentionTest, LongshotReservationUsesByteLimitInsteadOfOrdinaryPixelLimit) {
  EXPECT_EQ(store.publish(2, pixels(8)), kInvalidResultId);
  auto reservation = store.reserve(2, 8, 1, false);
  ASSERT_TRUE(reservation);
  EXPECT_NE(store.publish(2, pixels(8), std::move(reservation)), kInvalidResultId);
  EXPECT_FALSE(store.reserve(3, 9, 1, false));
}

TEST_F(ResultRetentionTest, ExplicitReservationAccountsForLongshotSpareCapacity) {
  Image padded = pixels();
  padded.pixels.reserve(8);
  const auto capacity_bytes = padded.pixels.capacity() * sizeof(std::uint32_t);
  auto reservation = store.reserve(2, padded.width, padded.height, false,
                                   capacity_bytes);
  ASSERT_TRUE(reservation);

  EXPECT_NE(store.publish(2, std::move(padded), std::move(reservation)),
            kInvalidResultId);
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, capacity_bytes);
}

TEST_F(ResultRetentionTest, SpareCapacityIsChargedAndPublicationFailurePreservesOldResult) {
  Image padded = pixels(1);
  padded.pixels.reserve(8);
  const auto capacity_bytes = padded.pixels.capacity() * sizeof(std::uint32_t);
  const auto id = store.publish(2, std::move(padded));
  ASSERT_NE(id, kInvalidResultId);
  EXPECT_EQ(store.budgetSnapshot().external_retained_bytes, capacity_bytes);
  EXPECT_FALSE(store.reserve(2, 8, 1, false));
  EXPECT_TRUE(store.acquire(2, id));
  EXPECT_EQ(store.publish(2, pixels(9)), kInvalidResultId);
  EXPECT_TRUE(store.acquire(2, id));
}

TEST_F(ResultRetentionTest, ExpiredReleaseReclaimsSlotAndRetainsScopedFailureReason) {
  const auto id = store.publish(2, pixels());
  now += limits.result_ttl;
  EXPECT_EQ(store.releaseResult(2, id), ErrorCode::kResultExpired);
  EXPECT_EQ(store.releaseResult(2, id), ErrorCode::kResultExpired);
  EXPECT_EQ(store.releaseResult(3, id), ErrorCode::kResultNotFound);
  EXPECT_EQ(store.budgetSnapshot().retained_bytes, 0u);
  now += limits.tombstone_ttl;
  EXPECT_EQ(store.resultStatus(2, id), ErrorCode::kResultNotFound);
}

TEST_F(ResultRetentionTest, SlotLimitFailureRollsBackNewReservationWithoutEviction) {
  for (ResultScopeId scope = 2; scope < 6; ++scope) {
    ASSERT_NE(store.publish(scope, pixels(1)), kInvalidResultId);
  }
  EXPECT_EQ(store.publish(6, pixels(1)), kInvalidResultId);
  EXPECT_EQ(store.budgetSnapshot().reserved_bytes, 0u);
  EXPECT_EQ(store.budgetSnapshot().retained_bytes, 16u);
  for (ResultScopeId scope = 2; scope < 6; ++scope) {
    EXPECT_TRUE(store.acquire(scope, ResultSelection::current()));
  }
}

TEST_F(ResultRetentionTest, TombstonesAreScopedBoundedAndExpireWithoutPixels) {
  const auto first = store.publish(2, pixels());
  store.releaseResult(2, first);
  for (int i = 0; i < 4; ++i) {
    const auto id = store.publish(2, pixels());
    EXPECT_EQ(store.releaseResult(2, id), ErrorCode::kOk);
  }
  EXPECT_EQ(store.tombstoneCount(), 2u);
  EXPECT_EQ(store.releaseResult(2, first), ErrorCode::kResultNotFound);
  EXPECT_EQ(store.budgetSnapshot().retained_bytes, 0u);
  now += limits.tombstone_ttl;
  store.sweep();
  EXPECT_EQ(store.tombstoneCount(), 0u);
}

TEST_F(ResultRetentionTest, GlobalTombstoneBoundAndDelayedSweepDoNotExtendRetention) {
  for (ResultScopeId scope = 2; scope < 30; ++scope) {
    const auto id = store.publish(scope, pixels());
    ASSERT_NE(id, kInvalidResultId);
    store.releaseResult(scope, id);
  }
  EXPECT_LE(store.tombstoneCount(), limits.max_connections * limits.max_tombstones_per_connection);
  const auto id = store.publish(2, pixels());
  now += limits.result_ttl + limits.tombstone_ttl;
  store.sweep();
  EXPECT_EQ(store.tombstoneCount(), 0u);
  EXPECT_EQ(store.resultStatus(2, id), ErrorCode::kResultNotFound);
}

TEST_F(ResultRetentionTest, LeaseReturnsBudgetOnWorkerAfterStoreDestruction) {
  auto owner = std::make_unique<ResultStore>(limits);
  const auto observer = owner->budgetObserver();
  const auto id = owner->publish(2, pixels());
  auto held = owner->acquire(2, id);
  owner.reset();
  EXPECT_EQ(observer.snapshot().external_retained_bytes, 16u);
  std::thread worker([lease = std::move(held)]() mutable { lease = {}; });
  worker.join();
  EXPECT_EQ(observer.snapshot().external_retained_bytes, 0u);
}

TEST(ResultStoreDeadlineTest, SaturatesDeadlineWithoutClockArithmeticOverflow) {
  auto now = (std::chrono::steady_clock::time_point::max)() - std::chrono::seconds{1};
  ResultStore store({}, [&] { return now; });
  const auto id = store.publish(2, Image{1, 1, {1}});
  ASSERT_NE(id, kInvalidResultId);
  EXPECT_EQ(store.expiresIn(2, id), std::chrono::milliseconds{1000});
  now = (std::chrono::steady_clock::time_point::max)();
  EXPECT_FALSE(store.acquire(2, id));
  store.sweep();
}

TEST(ResultStoreDeadlineTest, HugeInjectedTtlDoesNotOverflowDurationConversion) {
  AutomationLimits limits;
  limits.result_ttl = (std::chrono::milliseconds::max)();
  ResultStore store(limits, [] { return std::chrono::steady_clock::time_point{}; });
  const auto id = store.publish(2, Image{1, 1, {1}});
  ASSERT_NE(id, kInvalidResultId);
  EXPECT_EQ(store.acquire(2, id).metadata().expires_at,
            (std::chrono::steady_clock::time_point::max)());
}
}  // namespace qingying

namespace qingying {
TEST(ResultStoreTest, ScopesRejectForeignIdsAndClearIndependently) {
  ResultStore store;
  const auto gui = store.publish(Image{1, 1, {11}});
  const auto a = store.publish(2, Image{1, 1, {22}});
  const auto b = store.publish(3, Image{1, 1, {33}});
  EXPECT_FALSE(store.acquire(2, b));
  EXPECT_FALSE(store.acquire(3, gui));
  EXPECT_FALSE(store.acquire(kGuiResultScopeId, a));
  EXPECT_FALSE(store.get(a));
  store.release(2, b);
  EXPECT_TRUE(store.acquire(3, b));
  store.clear();
  EXPECT_FALSE(store.acquire(kGuiResultScopeId, gui));
  EXPECT_EQ(store.acquire(2, ResultSelection::current()).image()->pixels[0], 22u);
  store.clearScope(2);
  EXPECT_FALSE(store.acquire(2, a));
  EXPECT_TRUE(store.acquire(3, b));
  store.clearAll();
  EXPECT_FALSE(store.acquire(3, b));
}
TEST(ResultStoreTest, LeaseSurvivesReplacementReleaseAndStoreDestruction) {
  ResultLease retained;
  {
    ResultStore store;
    const auto id = store.publish(2, Image{2, 1, {11, 22}}, {-5, 7, 2, 1});
    retained = store.acquire(2, id);
    ASSERT_TRUE(retained);
    EXPECT_EQ(retained.image(), store.acquire(2, id).image());
    const auto next = store.publish(2, Image{1, 1, {33}});
    EXPECT_FALSE(store.acquire(2, id));
    store.release(2, next);
    EXPECT_EQ(retained.image()->pixels[1], 22u);
  }
  ASSERT_TRUE(retained);
  EXPECT_EQ(retained.image()->pixels[0], 11u);
  EXPECT_EQ(retained.metadata().bounds, (ScreenPhysicalRect{-5, 7, 2, 1}));
  EXPECT_EQ(retained.metadata().width, 2);
}
TEST(ResultStoreTest, InvalidScopeOrSelectionCannotAccessOrPublish) {
  ResultStore store;
  EXPECT_EQ(store.publish(0, Image{1, 1, {1}}), kInvalidResultId);
  const auto id = store.publish(2, Image{1, 1, {2}});
  EXPECT_FALSE(store.acquire(0, id));
  EXPECT_FALSE(store.acquire(2, ResultSelection{ResultSelectionKind::Current, id}));
  EXPECT_EQ(store.publish(2, Image{2, 1, {1}}), kInvalidResultId);
  EXPECT_TRUE(store.acquire(2, id));
}
}  // namespace qingying
