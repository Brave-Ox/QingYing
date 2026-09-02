#include "qingying/longshot/longshot_plugin_host.h"

#include <Windows.h>

#include <gtest/gtest.h>

#include <string>

namespace qingying {
namespace {

std::wstring testPluginPath() {
  wchar_t buffer[32768] = {};
  const DWORD length = GetModuleFileNameW(nullptr, buffer,
                                          static_cast<DWORD>(sizeof(buffer) /
                                                             sizeof(buffer[0])));
  if (length == 0 || length >= sizeof(buffer) / sizeof(buffer[0])) {
    return {};
  }

  std::wstring path(buffer, length);
  const std::wstring::size_type separator = path.find_last_of(L"\\/");
  if (separator == std::wstring::npos) {
    return {};
  }
  return path.substr(0, separator + 1) + L"qingying_test_longshot_plugin.dll";
}

}  // namespace

TEST(LongShotPluginHostTest, LoadsAndValidatesVersionOnePlugin) {
  const std::wstring path = testPluginPath();
  ASSERT_FALSE(path.empty());

  LongShotPluginHost host;
  ASSERT_TRUE(host.loadFile(path));
  ASSERT_EQ(host.plugins().size(), 1u);

  const auto& plugin = *host.plugins().front();
  EXPECT_STREQ(plugin.api().id_utf8, "test.longshot");
  EXPECT_EQ(plugin.api().abi_version,
            QINGYING_LONGSHOT_PLUGIN_ABI_VERSION_V1);
  EXPECT_EQ(plugin.api().priority, 1);

  EXPECT_FALSE(host.loadFile(path));
  host.unloadAll();
  EXPECT_TRUE(host.plugins().empty());
}

TEST(LongShotPluginHostTest, MissingDirectoryIsAnEmptyLoad) {
  LongShotPluginHost host;

  EXPECT_EQ(host.loadDirectory(
                L"C:\\qingying-longshot-directory-that-does-not-exist"),
            0u);
  EXPECT_TRUE(host.plugins().empty());
}

}  // namespace qingying
