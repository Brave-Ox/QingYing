#include "builtin_longshot_plugin_support.h"

#include "qingying/longshot/notepad_longshot_profile.hpp"

namespace {

qingying::builtin_longshot_plugin::PluginContext g_context;

bool resolveNotepadRequest(const qingying::LongShotRequest& request,
                           qingying::LongShotProfileResult& out) {
  if (!qingying::resolveNotepadProfile(request.owner_window, out)) {
    return false;
  }
  return out.containsSelection(request.x, request.y, request.width,
                               request.height);
}

}  // namespace

extern "C" QINGYING_LONGSHOT_PLUGIN_EXPORT int32_t
QINGYING_LONGSHOT_PLUGIN_CALL qingying_longshot_plugin_entry_v1(
    const QingYingLongShotHostV1* host,
    QingYingLongShotPluginV1* plugin) {
  return qingying::builtin_longshot_plugin::initializePlugin(
      host, plugin, g_context, "builtin.notepad", "QingYing Notepad LongShot",
      100, &resolveNotepadRequest);
}

