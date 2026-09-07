#include "qingying/app/result_budget.h"

#include <gtest/gtest.h>

#include <limits>
#include <thread>
#include <vector>

namespace qingying {
namespace {
AutomationLimits smallBudget() {
  AutomationLimits limits;
  limits.max_capture_pixels = 4;
  limits.max_result_bytes = 32;
  limits.max_retained_result_bytes = 48;
  return limits;
}
}  // namespace

TEST(ResultBudgetTest, OrdinaryPixelAndLongshotByteBoundaries) {
  ResultBudget budget(smallBudget());
  EXPECT_FALSE(budget.reserve(2, 0, 1));
  EXPECT_FALSE(budget.reserve(2, -1, 1));
  EXPECT_FALSE(budget.reserve(0, 1, 1));
  EXPECT_FALSE(budget.reserve(2, 5, 1));
  EXPECT_TRUE(budget.reserve(2, 4, 1));
  EXPECT_TRUE(budget.reserve(2, 8, 1, false));
  EXPECT_FALSE(budget.reserve(2, 9, 1, false));
  EXPECT_EQ(budget.snapshot().reserved_bytes, 0u);
}

TEST(ResultBudgetTest, DefaultOrdinaryCaptureLimitIsExactWithoutAllocatingPixels) {
  ResultBudget budget;
  EXPECT_TRUE(budget.reserve(2, 4096, 4096));
  EXPECT_FALSE(budget.reserve(2, 16777217, 1));
  EXPECT_TRUE(budget.reserve(2, 33554432, 1, false));
  EXPECT_FALSE(budget.reserve(2, 33554433, 1, false));
  EXPECT_EQ(budget.snapshot().reserved_bytes, 0u);
}

TEST(ResultBudgetTest, TotalIncludesReservationsAndRollbackRestoresCapacity) {
  ResultBudget budget(smallBudget());
  auto a = budget.reserve(2, 8, 1, false);
  auto b = budget.reserve(3, 4, 1);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  EXPECT_EQ(budget.snapshot().external_reserved_bytes, 48u);
  EXPECT_FALSE(budget.reserve(4, 1, 1));
  a = {};
  EXPECT_EQ(budget.snapshot().external_reserved_bytes, 16u);
  EXPECT_TRUE(budget.reserve(4, 8, 1, false));
}

TEST(ResultBudgetTest, MoveTransfersSingleChargeAndGuiDoesNotUseExternalQuota) {
  ResultBudget budget(smallBudget());
  auto a = budget.reserve(2, 4, 1);
  auto b = std::move(a);
  EXPECT_FALSE(a);
  EXPECT_EQ(budget.snapshot().reserved_bytes, 16u);
  auto gui = budget.reserve(kGuiResultScopeId, 100, 1);
  ASSERT_TRUE(gui);
  EXPECT_EQ(budget.snapshot().reserved_bytes, 416u);
  EXPECT_EQ(budget.snapshot().external_reserved_bytes, 16u);
  b = std::move(gui);
  EXPECT_EQ(budget.snapshot().external_reserved_bytes, 0u);
  EXPECT_EQ(budget.snapshot().reserved_bytes, 400u);
}

TEST(ResultBudgetTest, CheckedArithmeticRejectsOverflowAndSpareCapacityExcess) {
  ResultBudget budget(smallBudget());
  const auto max = (std::numeric_limits<std::uint64_t>::max)();
  EXPECT_FALSE(budget.reserve(2, (std::numeric_limits<int>::max)(),
                                 (std::numeric_limits<int>::max)()));
  EXPECT_FALSE(budget.reserve(2, 1, 1, true, max));
  EXPECT_FALSE(budget.reserve(2, 2, 1, true, 4));
  auto all = budget.reserve(kGuiResultScopeId, 1, 1, true, max);
  ASSERT_TRUE(all);
  EXPECT_FALSE(budget.reserve(kGuiResultScopeId, 1, 1));
  EXPECT_FALSE(budget.reserve(2, 1, 1));
  all = {};
  EXPECT_EQ(budget.snapshot().reserved_bytes, 0u);
}

TEST(ResultBudgetTest, ReservationsCanReturnConcurrentlyAfterOwnerDestruction) {
  auto budget = std::make_unique<ResultBudget>(smallBudget());
  const auto observer = *budget;
  std::vector<std::thread> workers;
  for (int i = 0; i < 4; ++i) {
    auto reservation = budget->reserve(2, 1, 1);
    ASSERT_TRUE(reservation);
    workers.emplace_back([held = std::move(reservation)]() mutable { held = {}; });
  }
  budget.reset();
  for (auto& worker : workers) worker.join();
  EXPECT_EQ(observer.snapshot().reserved_bytes, 0u);
}

}  // namespace qingying
