#include "qingying/longshot/longshot_plugin_host.h"

#include <Windows.h>

#include <gtest/gtest.h>

#include <set>
#include <string>

namespace qingying {
namespace {

std::wstring testPluginDirectory() {
  wchar_t buffer[32768] = {};
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer, static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])));
  if (length == 0 || length >= sizeof(buffer) / sizeof(buffer[0])) {
    return {};
  }

  const std::wstring path(buffer, length);
  const std::wstring::size_type separator = path.find_last_of(L"\\/");
  if (separator == std::wstring::npos) {
    return {};
  }
  return path.substr(0, separator + 1) + L"plugins\\longshot";
}

}  // namespace

TEST(BuiltinLongShotPluginTest, LoadsPackagedNotepadAndExplorerProfiles) {
  LongShotPluginHost host(testPluginDirectory());

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

