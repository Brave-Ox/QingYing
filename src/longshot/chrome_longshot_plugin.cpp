#include "builtin_longshot_plugin_support.h"

#include "chrome_longshot_resolver.h"

namespace {

qingying::builtin_longshot_plugin::PluginContext g_context;

bool resolveChromeRequest(const qingying::LongShotRequest& request,
                          qingying::LongShotProfileResult& out) {
  return qingying::longshot_detail::resolveChromeTarget(request.owner_window,
                                                         out) &&
         out.containsSelection(request.x, request.y, request.width,
                               request.height);
}

}  // namespace

extern "C" QINGYING_LONGSHOT_PLUGIN_EXPORT int32_t
QINGYING_LONGSHOT_PLUGIN_CALL qingying_longshot_plugin_entry_v1(
    const QingYingLongShotHostV1* host,
    QingYingLongShotPluginV1* plugin) {
  return qingying::builtin_longshot_plugin::initializePlugin(
      host, plugin, g_context, "builtin.chrome", "QingYing Chrome LongShot",
      100, &resolveChromeRequest, false);
}
