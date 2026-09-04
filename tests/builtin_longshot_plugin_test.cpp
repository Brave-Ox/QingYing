#include "builtin_longshot_test_helpers.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace qingying {

TEST(BuiltinLongShotPluginTest, LoadsPackagedBuiltinProfiles) {
  LongShotPluginHost host(test_support::builtinLongShotPluginDirectory());

  ASSERT_EQ(host.loadDirectory(), 3u);
  ASSERT_EQ(host.plugins().size(), 3u);

  std::set<std::string> ids;
  for (const auto& plugin : host.plugins()) {
    ASSERT_NE(plugin, nullptr);
    ASSERT_NE(plugin->api().id_utf8, nullptr);
    ids.insert(plugin->api().id_utf8);
    if (std::string(plugin->api().id_utf8) == "builtin.browser") {
      EXPECT_EQ(plugin->api().capabilities, 0u);
      EXPECT_EQ(plugin->api().query_scroll_state, nullptr);
    } else {
      EXPECT_EQ(plugin->api().capabilities,
                QINGYING_LONGSHOT_CAP_NATIVE_SCROLL_STATE);
      EXPECT_NE(plugin->api().query_scroll_state, nullptr);
    }
  }

  EXPECT_EQ(ids, (std::set<std::string>{"builtin.browser", "builtin.explorer",
                                         "builtin.notepad"}));
}

}  // namespace qingying
