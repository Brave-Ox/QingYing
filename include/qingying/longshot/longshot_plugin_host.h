#pragma once

#include "qingying/longshot/longshot_plugin_api.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace qingying {

// Loads long-shot profile DLLs and keeps their modules resident. The host does
// not translate a plugin into a LongShotProfile yet; DllLongShotProfile will
// consume the loaded descriptors in the next layer.
class LongShotPluginHost {
 public:
  class LoadedPlugin {
   public:
    ~LoadedPlugin() = default;

    LoadedPlugin(const LoadedPlugin&) = delete;
    LoadedPlugin& operator=(const LoadedPlugin&) = delete;

    const std::wstring& path() const noexcept { return path_; }
    const QingYingLongShotPluginV1& api() const noexcept { return api_; }

   private:
    friend class LongShotPluginHost;

    LoadedPlugin(std::wstring path, void* module,
                 QingYingLongShotPluginV1 api)
        : path_(std::move(path)), module_(module), api_(api) {}

    std::wstring path_;
    void* module_{nullptr};
    QingYingLongShotPluginV1 api_{};
  };

  using PluginList = std::vector<std::unique_ptr<LoadedPlugin>>;

  LongShotPluginHost();
  explicit LongShotPluginHost(std::wstring plugin_directory);
  ~LongShotPluginHost();

  LongShotPluginHost(const LongShotPluginHost&) = delete;
  LongShotPluginHost& operator=(const LongShotPluginHost&) = delete;
  LongShotPluginHost(LongShotPluginHost&&) = delete;
  LongShotPluginHost& operator=(LongShotPluginHost&&) = delete;

  void setPluginDirectory(std::wstring plugin_directory);
  const std::wstring& pluginDirectory() const noexcept;

  // Loads every *.dll in the configured directory in deterministic filename
  // order. A failed or incompatible DLL is skipped; the return value is the
  // number of newly loaded plugins.
  std::size_t loadDirectory();
  std::size_t loadDirectory(const std::wstring& plugin_directory);

  // Loads one DLL. The module remains loaded until unloadAll() or destruction.
  // Loading the same path or a duplicate plugin id is rejected.
  bool loadFile(const std::wstring& plugin_path);

  // Must be called only after all users of the loaded descriptors and their
  // callback code have stopped. The operation is idempotent.
  void unloadAll() noexcept;

  const PluginList& plugins() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qingying
