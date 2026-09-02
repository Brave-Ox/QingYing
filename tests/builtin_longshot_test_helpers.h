#pragma once

#include "qingying/longshot/dll_longshot_profile.h"

#include <Windows.h>

#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace qingying::test_support {

inline std::wstring builtinLongShotPluginDirectory() {
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

inline const LongShotPluginHost::LoadedPlugin* findBuiltinLongShotPlugin(
    const LongShotPluginHost& host, const char* plugin_id) {
  if (plugin_id == nullptr) {
    return nullptr;
  }

  for (const auto& plugin : host.plugins()) {
    if (plugin == nullptr || plugin->api().id_utf8 == nullptr) {
      continue;
    }
    if (std::strcmp(plugin->api().id_utf8, plugin_id) == 0) {
      return plugin.get();
    }
  }
  return nullptr;
}

class BuiltinLongShotProfileContext final {
 public:
  explicit BuiltinLongShotProfileContext(const char* plugin_id)
      : host_(builtinLongShotPluginDirectory()) {
    if (host_.loadDirectory() == 0) {
      return;
    }

    const LongShotPluginHost::LoadedPlugin* plugin =
        findBuiltinLongShotPlugin(host_, plugin_id);
    if (plugin != nullptr) {
      profile_ = std::make_unique<DllLongShotProfile>(*plugin);
    }
  }

  BuiltinLongShotProfileContext(const BuiltinLongShotProfileContext&) = delete;
  BuiltinLongShotProfileContext& operator=(
      const BuiltinLongShotProfileContext&) = delete;

  bool ready() const noexcept { return profile_ != nullptr; }

  LongShotProfile& profile() { return *profile_; }

  LongShotProfileRegistry takeRegistry() {
    LongShotProfileRegistry registry;
    if (profile_ != nullptr) {
      registry.add(std::move(profile_));
    }
    return registry;
  }

 private:
  LongShotPluginHost host_;
  std::unique_ptr<DllLongShotProfile> profile_;
};

}  // namespace qingying::test_support
