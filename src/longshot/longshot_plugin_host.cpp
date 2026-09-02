#include "qingying/longshot/longshot_plugin_host.h"

#include "qingying/longshot/longshot_profile.hpp"
#include "win32_scroll_helpers.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace qingying {

namespace {

using PluginApi = QingYingLongShotPluginV1;

constexpr std::uint32_t kRequiredPluginStructSize = static_cast<std::uint32_t>(
    offsetof(PluginApi, shutdown) + sizeof(QingYingLongShotShutdownFnV1));

bool hasStructSize(std::uint32_t actual, std::size_t required) {
  return actual >= required;
}

HWND toWindow(std::uint64_t value) {
  if (value == 0 ||
      value > static_cast<std::uint64_t>(
                  (std::numeric_limits<std::uintptr_t>::max)())) {
    return nullptr;
  }
  return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(value));
}

bool isDescendantWindow(HWND owner, HWND candidate) {
  if (owner == nullptr || candidate == nullptr || !IsWindow(owner) ||
      !IsWindow(candidate)) {
    return false;
  }
  return owner == candidate || IsChild(owner, candidate) != FALSE;
}

bool convertRequest(const QingYingLongShotRequestV1& source,
                    LongShotRequest& target) {
  target = LongShotRequest{};
  if (!hasStructSize(source.struct_size, sizeof(source))) {
    return false;
  }

  target.owner_window = static_cast<std::uintptr_t>(source.owner_window);
  target.x = source.x;
  target.y = source.y;
  target.width = source.width;
  target.height = source.height;
  return target.valid();
}

bool convertTarget(const QingYingLongShotTargetV1& source,
                   LongShotProfileResult& target) {
  target = LongShotProfileResult{};
  if (!hasStructSize(source.struct_size, sizeof(source)) ||
      source.scroll_target >
          static_cast<std::uint64_t>(
              (std::numeric_limits<std::uintptr_t>::max)())) {
    return false;
  }

  target.scroll_target = static_cast<std::uintptr_t>(source.scroll_target);
  target.content_x = source.content_x;
  target.content_y = source.content_y;
  target.content_width = source.content_width;
  target.content_height = source.content_height;
  return target.valid();
}

std::int32_t QINGYING_LONGSHOT_PLUGIN_CALL hostIsDescendant(
    void* /*user_data*/, std::uint64_t owner_window,
    std::uint64_t candidate_window) {
  return isDescendantWindow(toWindow(owner_window), toWindow(candidate_window))
             ? 1
             : 0;
}

std::int32_t QINGYING_LONGSHOT_PLUGIN_CALL hostSendWheelDown(
    void* /*user_data*/, const QingYingLongShotRequestV1* request,
    const QingYingLongShotTargetV1* target) {
  if (request == nullptr || target == nullptr) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  LongShotRequest core_request;
  LongShotProfileResult core_target;
  if (!convertRequest(*request, core_request) ||
      !convertTarget(*target, core_target) ||
      !isDescendantWindow(toWindow(request->owner_window),
                          toWindow(target->scroll_target))) {
    return QINGYING_LONGSHOT_STATUS_NOT_SUPPORTED;
  }

  return longshot_detail::sendWheelDown(core_request, core_target)
             ? QINGYING_LONGSHOT_STATUS_OK
             : QINGYING_LONGSHOT_STATUS_FAILED;
}

std::int32_t QINGYING_LONGSHOT_PLUGIN_CALL hostQueryScrollState(
    void* /*user_data*/, const QingYingLongShotTargetV1* target,
    QingYingLongShotScrollStateV1* state) {
  if (target == nullptr || state == nullptr ||
      !hasStructSize(state->struct_size, sizeof(*state))) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  state->flags = 0;
  state->position = 0;
  state->last_position = 0;

  LongShotProfileResult core_target;
  if (!convertTarget(*target, core_target)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  LongShotScrollState core_state;
  if (!longshot_detail::queryVerticalScrollState(core_target, core_state)) {
    return QINGYING_LONGSHOT_STATUS_NOT_SUPPORTED;
  }

  state->flags = QINGYING_LONGSHOT_SCROLL_STATE_VALID;
  state->position = core_state.position;
  state->last_position = core_state.last_position;
  if (core_state.atBottom()) {
    state->flags |= QINGYING_LONGSHOT_SCROLL_STATE_AT_BOTTOM;
  }
  return QINGYING_LONGSHOT_STATUS_OK;
}

void QINGYING_LONGSHOT_PLUGIN_CALL hostLog(void* /*user_data*/, int32_t level,
                                           const char* message_utf8) {
  if (message_utf8 == nullptr) {
    return;
  }

  OutputDebugStringA("[QingYing LongShot Plugin] ");
  if (level != 0) {
    OutputDebugStringA("[level] ");
  }
  OutputDebugStringA(message_utf8);
  OutputDebugStringA("\n");
}

QingYingLongShotHostV1 makeHostApi() {
  QingYingLongShotHostV1 host{};
  host.struct_size = sizeof(host);
  host.abi_version = QINGYING_LONGSHOT_HOST_ABI_VERSION_V1;
  host.log = &hostLog;
  host.is_descendant = &hostIsDescendant;
  host.send_wheel_down = &hostSendWheelDown;
  host.query_scroll_state = &hostQueryScrollState;
  return host;
}

std::wstring makeFullPath(const std::wstring& path) {
  if (path.empty()) {
    return {};
  }

  std::vector<wchar_t> buffer(512, L'\0');
  for (;;) {
    const DWORD length = GetFullPathNameW(path.c_str(),
                                          static_cast<DWORD>(buffer.size()),
                                          buffer.data(), nullptr);
    if (length == 0) {
      return {};
    }
    if (length < buffer.size()) {
      return std::wstring(buffer.data(), length);
    }
    buffer.resize(static_cast<std::size_t>(length) + 1, L'\0');
  }
}

std::wstring joinPath(const std::wstring& directory,
                      const std::wstring& file_name) {
  if (directory.empty()) {
    return file_name;
  }
  if (directory.back() == L'\\' || directory.back() == L'/') {
    return directory + file_name;
  }
  return directory + L'\\' + file_name;
}

bool hasValidPluginId(const PluginApi& plugin) {
  return plugin.id_utf8 != nullptr && plugin.id_utf8[0] != '\0';
}

bool hasRequiredCallbacks(const PluginApi& plugin) {
  if (plugin.probe == nullptr || plugin.open == nullptr ||
      plugin.resolve == nullptr || plugin.scroll_down == nullptr ||
      plugin.close_session == nullptr || plugin.shutdown == nullptr) {
    return false;
  }
  return (plugin.capabilities & QINGYING_LONGSHOT_CAP_NATIVE_SCROLL_STATE) ==
             0 ||
         plugin.query_scroll_state != nullptr;
}

bool isValidPlugin(const PluginApi& plugin) {
  return hasStructSize(plugin.struct_size, kRequiredPluginStructSize) &&
         plugin.abi_version == QINGYING_LONGSHOT_PLUGIN_ABI_VERSION_V1 &&
         hasValidPluginId(plugin) && hasRequiredCallbacks(plugin);
}

bool samePluginId(const PluginApi& left, const PluginApi& right) {
  return left.id_utf8 != nullptr && right.id_utf8 != nullptr &&
         _stricmp(left.id_utf8, right.id_utf8) == 0;
}

}  // namespace

struct LongShotPluginHost::Impl {
  std::wstring plugin_directory;
  PluginList plugins;
  QingYingLongShotHostV1 host_api{makeHostApi()};
};

LongShotPluginHost::LongShotPluginHost()
    : impl_(std::make_unique<Impl>()) {}

LongShotPluginHost::LongShotPluginHost(std::wstring plugin_directory)
    : impl_(std::make_unique<Impl>()) {
  impl_->plugin_directory = std::move(plugin_directory);
}

LongShotPluginHost::~LongShotPluginHost() {
  unloadAll();
}

void LongShotPluginHost::setPluginDirectory(std::wstring plugin_directory) {
  impl_->plugin_directory = std::move(plugin_directory);
}

const std::wstring& LongShotPluginHost::pluginDirectory() const noexcept {
  return impl_->plugin_directory;
}

std::size_t LongShotPluginHost::loadDirectory() {
  return loadDirectory(impl_->plugin_directory);
}

std::size_t LongShotPluginHost::loadDirectory(
    const std::wstring& plugin_directory) {
  setPluginDirectory(plugin_directory);
  if (plugin_directory.empty()) {
    return 0;
  }

  const std::wstring directory = makeFullPath(plugin_directory);
  if (directory.empty() ||
      GetFileAttributesW(directory.c_str()) == INVALID_FILE_ATTRIBUTES) {
    return 0;
  }

  const std::wstring pattern = joinPath(directory, L"*.dll");
  WIN32_FIND_DATAW find_data{};
  const HANDLE find_handle = FindFirstFileW(pattern.c_str(), &find_data);
  if (find_handle == INVALID_HANDLE_VALUE) {
    return 0;
  }

  std::vector<std::wstring> paths;
  do {
    if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
      paths.push_back(joinPath(directory, find_data.cFileName));
    }
  } while (FindNextFileW(find_handle, &find_data) != FALSE);
  FindClose(find_handle);

  std::sort(paths.begin(), paths.end(), [](const std::wstring& left,
                                           const std::wstring& right) {
    return _wcsicmp(left.c_str(), right.c_str()) < 0;
  });

  std::size_t loaded_count = 0;
  for (const auto& path : paths) {
    if (loadFile(path)) {
      ++loaded_count;
    }
  }
  return loaded_count;
}

bool LongShotPluginHost::loadFile(const std::wstring& plugin_path) {
  const std::wstring full_path = makeFullPath(plugin_path);
  if (full_path.empty()) {
    return false;
  }

  for (const auto& loaded : impl_->plugins) {
    if (loaded != nullptr &&
        _wcsicmp(loaded->path().c_str(), full_path.c_str()) == 0) {
      return false;
    }
  }

  const HMODULE module = LoadLibraryExW(
      full_path.c_str(), nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if (module == nullptr) {
    return false;
  }

  const FARPROC symbol = GetProcAddress(
      module, QINGYING_LONGSHOT_PLUGIN_ENTRY_SYMBOL_V1);
  if (symbol == nullptr) {
    FreeLibrary(module);
    return false;
  }

  const auto entry = reinterpret_cast<QingYingLongShotEntryFnV1>(symbol);
  PluginApi plugin{};
  plugin.struct_size = sizeof(plugin);
  const std::int32_t status = entry(&impl_->host_api, &plugin);
  if (status != QINGYING_LONGSHOT_STATUS_OK || !isValidPlugin(plugin)) {
    if (plugin.shutdown != nullptr) {
      plugin.shutdown(plugin.plugin_context);
    }
    FreeLibrary(module);
    return false;
  }

  for (const auto& loaded : impl_->plugins) {
    if (loaded != nullptr && samePluginId(loaded->api(), plugin)) {
      plugin.shutdown(plugin.plugin_context);
      FreeLibrary(module);
      return false;
    }
  }

  impl_->plugins.push_back(std::unique_ptr<LoadedPlugin>(
      new LoadedPlugin(full_path, reinterpret_cast<void*>(module),
                       &impl_->host_api, plugin)));
  return true;
}

void LongShotPluginHost::unloadAll() noexcept {
  if (impl_ == nullptr) {
    return;
  }

  for (auto it = impl_->plugins.rbegin(); it != impl_->plugins.rend(); ++it) {
    if (*it != nullptr) {
      LoadedPlugin& plugin = **it;
      if (plugin.api_.shutdown != nullptr) {
        plugin.api_.shutdown(plugin.api_.plugin_context);
        plugin.api_.shutdown = nullptr;
      }

      if (plugin.module_ != nullptr) {
        (void)FreeLibrary(reinterpret_cast<HMODULE>(plugin.module_));
        plugin.module_ = nullptr;
      }
      plugin.api_ = QingYingLongShotPluginV1{};
    }
  }
  impl_->plugins.clear();
}

const LongShotPluginHost::PluginList& LongShotPluginHost::plugins()
    const noexcept {
  return impl_->plugins;
}

}  // namespace qingying
