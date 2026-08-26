#include "qingying/app/longshot_request_adapter.hpp"

#include <gtest/gtest.h>

namespace qingying {

TEST(LongShotRequestAdapterTest, ConvertsScreenSelectionAndOwnerWindow) {
  SelectionResult selection;
  selection.cancelled = false;
  selection.x = -120;
  selection.y = 80;
  selection.width = 640;
  selection.height = 480;

  const LongShotRequest request =
      makeLongShotRequest(static_cast<std::uintptr_t>(0x1234), selection);

  EXPECT_EQ(request.owner_window, static_cast<std::uintptr_t>(0x1234));
  EXPECT_EQ(request.x, -120);
  EXPECT_EQ(request.y, 80);
  EXPECT_EQ(request.width, 640);
  EXPECT_EQ(request.height, 480);
  EXPECT_TRUE(request.valid());
}

TEST(LongShotRequestAdapterTest, CancelledSelectionProducesEmptyRequest) {
  SelectionResult selection;
  selection.cancelled = true;
  selection.width = 640;
  selection.height = 480;

  const LongShotRequest request = makeLongShotRequest(0x1234, selection);

  EXPECT_EQ(request.owner_window, 0u);
  EXPECT_EQ(request.x, 0);
  EXPECT_EQ(request.y, 0);
  EXPECT_EQ(request.width, 0);
  EXPECT_EQ(request.height, 0);
  EXPECT_FALSE(request.valid());
}

}  // namespace qingying
