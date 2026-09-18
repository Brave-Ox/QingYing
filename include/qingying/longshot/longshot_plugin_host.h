#pragma once

#include "qingying/longshot/longshot_plugin_api.h"
#include "qingying/diagnostics/fault_boundary.h"
#include <atomic>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace qingying {

// Loads long-shot profile DLLs and keeps their modules resident. Use
// addDllLongShotProfiles() to adapt the loaded descriptors to LongShotProfile.
class LongShotPluginHost {
 public:
  class LoadedPlugin {
   public:
    ~LoadedPlugin() = default;

    LoadedPlugin(const LoadedPlugin&) = delete;
    LoadedPlugin& operator=(const LoadedPlugin&) = delete;

    const std::wstring& path() const noexcept { return path_; }
    const QingYingLongShotPluginV1& api() const noexcept { return api_; }

    bool faulted() const noexcept { return faulted_.load(); }
    // C++ exception containment, not isolation from access violations or hangs.
    // Cleanup is still allowed after quarantine; normal callbacks are rejected.
    template <typename Function>
    std::int32_t invoke(Function&& function, bool cleanup = false) const noexcept {
      const auto started = std::chrono::steady_clock::now();
      const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(started.time_since_epoch()).count();
      if (!cleanup && (faulted() || milliseconds < cooldown_until_.load()))
        return QINGYING_LONGSHOT_STATUS_FAILED;
      std::int32_t status = QINGYING_LONGSHOT_STATUS_FAILED;
      if (!containFault(FaultOrigin::Plugin, FaultDomain::Provider,
          [&] { status = function(); }, nullptr, api_.id_utf8)) {
        faulted_.store(true);
        return QINGYING_LONGSHOT_STATUS_FAILED;
      }
      const auto elapsed = std::chrono::steady_clock::now() - started;
      if (!cleanup && elapsed >= std::chrono::milliseconds{500}) {
        cooldown_until_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() + 30000);
        auto context = currentFaultContext(); context.started_at = started;
        recordFault(ErrorCode::kTimeout, FaultOrigin::Plugin, FaultDomain::Provider, api_.id_utf8, context);
        return QINGYING_LONGSHOT_STATUS_FAILED;
      }
      return status;
    }

    bool isDescendant(std::uint64_t owner_window,
                      std::uint64_t candidate_window) const noexcept {
      return host_api_ != nullptr && host_api_->is_descendant != nullptr &&
             host_api_->is_descendant(host_api_->user_data, owner_window,
                                      candidate_window) != 0;
    }

   private:
    friend class LongShotPluginHost;

    LoadedPlugin(std::wstring path, void* module,
                 const QingYingLongShotHostV1* host_api,
                 QingYingLongShotPluginV1 api)
        : path_(std::move(path)),
          module_(module),
          host_api_(host_api),
          api_(api) {}

    std::wstring path_;
    void* module_{nullptr};
    const QingYingLongShotHostV1* host_api_{nullptr};
    QingYingLongShotPluginV1 api_{};
    mutable std::atomic<bool> faulted_{false};
    mutable std::atomic<std::int64_t> cooldown_until_{0};
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
