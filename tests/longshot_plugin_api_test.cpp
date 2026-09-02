#include "qingying/longshot/longshot_plugin_api.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace qingying {

TEST(LongShotPluginApiTest, KeepsVersionOneDataLayoutsStable) {
  EXPECT_EQ(sizeof(QingYingLongShotRequestV1), 32u);
  EXPECT_EQ(sizeof(QingYingLongShotTargetV1), 40u);
  EXPECT_EQ(sizeof(QingYingLongShotScrollStateV1), 24u);
  EXPECT_EQ(sizeof(QingYingLongShotProbeResultV1), 16u);
}

TEST(LongShotPluginApiTest, UsesExplicitVersionAndStatusConstants) {
  EXPECT_EQ(QINGYING_LONGSHOT_PLUGIN_ABI_VERSION_V1, 1u);
  EXPECT_EQ(QINGYING_LONGSHOT_HOST_ABI_VERSION_V1, 1u);
  EXPECT_STREQ(QINGYING_LONGSHOT_PLUGIN_ENTRY_SYMBOL_V1,
               "qingying_longshot_plugin_entry_v1");
  EXPECT_EQ(QINGYING_LONGSHOT_STATUS_OK, 0);
  EXPECT_EQ(QINGYING_LONGSHOT_STATUS_NO_MATCH, 1);
  EXPECT_EQ(QINGYING_LONGSHOT_STATUS_NOT_SUPPORTED, 2);
  EXPECT_EQ(QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT, 3);
  EXPECT_EQ(QINGYING_LONGSHOT_STATUS_RETRY, 4);
  EXPECT_EQ(QINGYING_LONGSHOT_STATUS_FAILED, 5);
}

TEST(LongShotPluginApiTest, SupportsAnOpaqueSessionAndEntryPoint) {
  QingYingLongShotRequestV1 request{};
  request.struct_size = sizeof(request);
  request.owner_window = static_cast<std::uint64_t>(0x1234);
  request.width = 640;
  request.height = 480;

  QingYingLongShotTargetV1 target{};
  target.struct_size = sizeof(target);
  target.scroll_target = static_cast<std::uint64_t>(0x5678);
  target.opaque_cookie = static_cast<std::uint64_t>(0x9abc);

  EXPECT_EQ(request.struct_size, sizeof(QingYingLongShotRequestV1));
  EXPECT_EQ(target.struct_size, sizeof(QingYingLongShotTargetV1));
  EXPECT_EQ(target.opaque_cookie, 0x9abcu);

  QingYingLongShotEntryFnV1 entry = nullptr;
  EXPECT_EQ(entry, nullptr);
}

}  // namespace qingying
