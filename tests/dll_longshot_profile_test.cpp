#include "qingying/longshot/dll_longshot_profile.h"

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

LongShotRequest testRequest() {
  return {reinterpret_cast<std::uintptr_t>(GetDesktopWindow()), 10, 20, 320,
          240};
}

}  // namespace

TEST(DllLongShotProfileTest, AdaptsPluginCallbacksToNativeProfile) {
  LongShotPluginHost host;
  ASSERT_TRUE(host.loadFile(testPluginPath()));
  ASSERT_EQ(host.plugins().size(), 1u);

  DllLongShotProfile profile(*host.plugins().front());
  EXPECT_STREQ(profile.name(), "test.longshot");

  const LongShotRequest request = testRequest();
  LongShotProfileResult result;
  ASSERT_TRUE(profile.resolve(request, result));
  EXPECT_EQ(result.scroll_target, request.owner_window);
  EXPECT_EQ(result.content.x, request.x);
  EXPECT_EQ(result.content.y, request.y);
  EXPECT_EQ(result.content.width, request.width);
  EXPECT_EQ(result.content.height, request.height);

  EXPECT_TRUE(profile.scrollDown(request, result));

  LongShotScrollState state;
  EXPECT_FALSE(profile.queryScrollState(result, state));
  EXPECT_FALSE(state.valid);
}

TEST(DllLongShotProfileTest, ConvertsAllLoadedPluginsIntoRegistryProfiles) {
  LongShotPluginHost host;
  ASSERT_TRUE(host.loadFile(testPluginPath()));

  LongShotProfileRegistry registry;
  EXPECT_EQ(addDllLongShotProfiles(host, registry), 1u);

  LongShotProfileResult result;
  const LongShotProfile* profile = registry.resolve(testRequest(), result);
  ASSERT_NE(profile, nullptr);
  EXPECT_STREQ(profile->name(), "test.longshot");
  EXPECT_TRUE(result.valid());
}

TEST(DllLongShotProfileTest, RejectsStaleOrOutOfBoundsProfileResults) {
  LongShotPluginHost host;
  ASSERT_TRUE(host.loadFile(testPluginPath()));

  DllLongShotProfile profile(*host.plugins().front());
  const LongShotRequest request = testRequest();
  LongShotProfileResult result;
  ASSERT_TRUE(profile.resolve(request, result));

  LongShotProfileResult stale = result;
  stale.content.x += 1;
  EXPECT_FALSE(profile.scrollDown(request, stale));

  LongShotRequest different_request = request;
  different_request.width += 1;
  EXPECT_FALSE(profile.scrollDown(different_request, result));
}

}  // namespace qingying
