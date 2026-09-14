#include "qingying/longshot/dll_longshot_profile.h"

#include "qingying/app/longshot_controller.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace qingying {
namespace {

std::wstring pluginPath(const wchar_t* file_name) {
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
  return path.substr(0, separator + 1) + file_name;
}

std::wstring testPluginPath() {
  return pluginPath(L"qingying_test_longshot_plugin.dll");
}

std::wstring blockingLegacyPluginPath() {
  return pluginPath(L"qingying_blocking_legacy_longshot_plugin.dll");
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

  profile.cancel();
  EXPECT_FALSE(profile.scrollDown(request, result));

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

TEST(DllLongShotProfileTest,
     KeepsLegacyBlockingPluginOwnerAliveAfterDeadlineMiss) {
  LongShotPluginHost host;
  ASSERT_TRUE(host.loadFile(blockingLegacyPluginPath()));
  ASSERT_EQ(host.plugins().size(), 1u);

  constexpr std::size_t kCancelFieldEnd =
      offsetof(QingYingLongShotPluginV1, cancel) +
      sizeof(QingYingLongShotCancelFnV1);
  EXPECT_LT(host.plugins().front()->api().struct_size, kCancelFieldEnd);

  using WaitEnteredFn = int32_t(QINGYING_LONGSHOT_PLUGIN_CALL*)(uint32_t);
  using ReleaseFn = void(QINGYING_LONGSHOT_PLUGIN_CALL*)();
  const HMODULE module = GetModuleHandleW(
      L"qingying_blocking_legacy_longshot_plugin.dll");
  ASSERT_NE(module, nullptr);
  const auto wait_entered = reinterpret_cast<WaitEnteredFn>(GetProcAddress(
      module, "qingying_test_blocking_longshot_wait_entered"));
  const auto release = reinterpret_cast<ReleaseFn>(GetProcAddress(
      module, "qingying_test_blocking_longshot_release"));
  ASSERT_NE(wait_entered, nullptr);
  ASSERT_NE(release, nullptr);

  LongShotProfileRegistry registry;
  ASSERT_EQ(addDllLongShotProfiles(host, registry), 1u);
  LongShotEngine engine(
      [](const ScreenPhysicalRect& region, Image& out) {
        out.width = region.width;
        out.height = region.height;
        out.pixels.assign(static_cast<std::size_t>(region.width) *
                              static_cast<std::size_t>(region.height),
                          0xFF112233u);
        ActionResult result;
        result.ok = true;
        result.error_code = ErrorCode::kOk;
        return result;
      },
      std::move(registry));
  SelectionOverlay overlay;
  LongShotController controller(engine, overlay);
  struct ReleaseGuard final {
    ReleaseFn release;
    ~ReleaseGuard() {
      if (release != nullptr) release();
    }
  } release_guard{release};

  const HWND owner_window = GetDesktopWindow();
  controller.setOwnerWindow(owner_window);
  const LongShotRequest request{reinterpret_cast<std::uintptr_t>(owner_window),
                                0, 0, 1, 1};
  ASSERT_TRUE(controller.start(request));
  ASSERT_EQ(wait_entered(1000), 1);

  controller.beginShutdown();
  EXPECT_FALSE(controller.joinUntil(std::chrono::steady_clock::now() +
                                    std::chrono::milliseconds(50)));
  EXPECT_TRUE(controller.active());
  const std::string snapshot = controller.diagnosticSnapshot();
  EXPECT_NE(snapshot.find("thread=longshot_worker"), std::string::npos);
  EXPECT_NE(snapshot.find("plugin_id=test.blocking.legacy"),
            std::string::npos);

  release();
  release_guard.release = nullptr;
  EXPECT_TRUE(controller.joinUntil(std::chrono::steady_clock::now() +
                                    std::chrono::seconds(2)));
  controller.finishShutdown();
  controller.shutdown();
  controller.shutdown();
  EXPECT_FALSE(controller.active());
}

}  // namespace qingying
