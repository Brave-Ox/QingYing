#include "builtin_longshot_plugin_support.h"

#include "explorer_longshot_resolver.h"

namespace {

qingying::builtin_longshot_plugin::PluginContext g_context;

bool resolveExplorerRequest(const qingying::LongShotRequest& request,
                            qingying::LongShotProfileResult& out) {
  return qingying::longshot_detail::resolveExplorerTarget(request, out) &&
         out.containsSelection(request.x, request.y, request.width,
                               request.height);
}

}  // namespace

extern "C" QINGYING_LONGSHOT_PLUGIN_EXPORT int32_t
QINGYING_LONGSHOT_PLUGIN_CALL qingying_longshot_plugin_entry_v1(
    const QingYingLongShotHostV1* host,
    QingYingLongShotPluginV1* plugin) {
  return qingying::builtin_longshot_plugin::initializePlugin(
      host, plugin, g_context, "builtin.explorer",
      "QingYing Explorer LongShot", 100, &resolveExplorerRequest);
}

