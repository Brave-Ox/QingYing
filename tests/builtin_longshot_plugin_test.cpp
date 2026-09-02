#include "builtin_longshot_test_helpers.h"

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace qingying {

TEST(BuiltinLongShotPluginTest, LoadsPackagedNotepadAndExplorerProfiles) {
  LongShotPluginHost host(test_support::builtinLongShotPluginDirectory());

  ASSERT_EQ(host.loadDirectory(), 2u);
  ASSERT_EQ(host.plugins().size(), 2u);

  std::set<std::string> ids;
  for (const auto& plugin : host.plugins()) {
    ASSERT_NE(plugin, nullptr);
    ASSERT_NE(plugin->api().id_utf8, nullptr);
    ids.insert(plugin->api().id_utf8);
    EXPECT_EQ(plugin->api().capabilities,
              QINGYING_LONGSHOT_CAP_NATIVE_SCROLL_STATE);
    EXPECT_NE(plugin->api().query_scroll_state, nullptr);
  }

  EXPECT_EQ(ids,
            (std::set<std::string>{"builtin.explorer", "builtin.notepad"}));
}

}  // namespace qingying
