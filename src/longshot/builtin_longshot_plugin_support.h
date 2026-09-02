#pragma once

#include "qingying/longshot/longshot_plugin_api.h"
#include "qingying/longshot/longshot_profile.hpp"

#include <cstdint>

namespace qingying::builtin_longshot_plugin {

using ResolveFunction = bool (*)(const LongShotRequest& request,
                                 LongShotProfileResult& out);

struct PluginContext {
  const QingYingLongShotHostV1* host{nullptr};
  ResolveFunction resolve{nullptr};
  const char* id_utf8{nullptr};
  const char* display_name_utf8{nullptr};
  std::int32_t priority{0};
};

std::int32_t initializePlugin(const QingYingLongShotHostV1* host,
                              QingYingLongShotPluginV1* plugin,
                              PluginContext& context, const char* id_utf8,
                              const char* display_name_utf8,
                              std::int32_t priority,
                              ResolveFunction resolve);

}  // namespace qingying::builtin_longshot_plugin

struct QingYingLongShotSessionV1 {
  qingying::builtin_longshot_plugin::PluginContext* context{nullptr};
  qingying::LongShotRequest request{};
  qingying::LongShotProfileResult profile{};
  QingYingLongShotTargetV1 target{};
  bool has_target{false};
};

