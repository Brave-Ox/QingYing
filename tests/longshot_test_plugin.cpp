#include "qingying/longshot/longshot_plugin_api.h"

struct QingYingLongShotSessionV1 {
  int marker{0};
};

namespace {

QingYingLongShotSessionV1 g_session;

int32_t QINGYING_LONGSHOT_PLUGIN_CALL probe(
    void* /*plugin_context*/, const QingYingLongShotRequestV1* request,
    QingYingLongShotProbeResultV1* result) {
  if (request == nullptr || result == nullptr ||
      request->struct_size < sizeof(*request) ||
      result->struct_size < sizeof(*result)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }
  result->flags = QINGYING_LONGSHOT_PROBE_ACCEPTED;
  result->score = 10;
  return QINGYING_LONGSHOT_STATUS_OK;
}

int32_t QINGYING_LONGSHOT_PLUGIN_CALL open(
    void* /*plugin_context*/, const QingYingLongShotRequestV1* request,
    QingYingLongShotSessionV1** session) {
  if (request == nullptr || session == nullptr ||
      request->struct_size < sizeof(*request)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }
  *session = &g_session;
  return QINGYING_LONGSHOT_STATUS_OK;
}

int32_t QINGYING_LONGSHOT_PLUGIN_CALL resolve(
    QingYingLongShotSessionV1* session,
    const QingYingLongShotRequestV1* request,
    QingYingLongShotTargetV1* target) {
  if (session == nullptr || request == nullptr || target == nullptr ||
      request->struct_size < sizeof(*request) ||
      target->struct_size < sizeof(*target)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }
  target->flags = QINGYING_LONGSHOT_TARGET_CONTENT_RECT_EXACT;
  target->scroll_target = request->owner_window;
  target->content_x = request->x;
  target->content_y = request->y;
  target->content_width = request->width;
  target->content_height = request->height;
  target->opaque_cookie = 0x1234u;
  return QINGYING_LONGSHOT_STATUS_OK;
}

int32_t QINGYING_LONGSHOT_PLUGIN_CALL scrollDown(
    QingYingLongShotSessionV1* session,
    const QingYingLongShotRequestV1* request,
    const QingYingLongShotTargetV1* target) {
  if (session == nullptr || request == nullptr || target == nullptr) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }
  return QINGYING_LONGSHOT_STATUS_OK;
}

int32_t QINGYING_LONGSHOT_PLUGIN_CALL queryScrollState(
    QingYingLongShotSessionV1* /*session*/,
    const QingYingLongShotTargetV1* /*target*/,
    QingYingLongShotScrollStateV1* /*state*/) {
  return QINGYING_LONGSHOT_STATUS_NOT_SUPPORTED;
}

void QINGYING_LONGSHOT_PLUGIN_CALL closeSession(
    QingYingLongShotSessionV1* /*session*/) {}

void QINGYING_LONGSHOT_PLUGIN_CALL shutdown(void* /*plugin_context*/) {}

}  // namespace

extern "C" QINGYING_LONGSHOT_PLUGIN_EXPORT int32_t
QINGYING_LONGSHOT_PLUGIN_CALL qingying_longshot_plugin_entry_v1(
    const QingYingLongShotHostV1* host,
    QingYingLongShotPluginV1* plugin) {
  if (host == nullptr || plugin == nullptr ||
      host->struct_size < sizeof(*host) ||
      host->abi_version != QINGYING_LONGSHOT_HOST_ABI_VERSION_V1 ||
      plugin->struct_size < sizeof(*plugin)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  plugin->abi_version = QINGYING_LONGSHOT_PLUGIN_ABI_VERSION_V1;
  plugin->priority = 1;
  plugin->capabilities = 0;
  plugin->id_utf8 = "test.longshot";
  plugin->display_name_utf8 = "QingYing Test LongShot";
  plugin->plugin_context = nullptr;
  plugin->probe = &probe;
  plugin->open = &open;
  plugin->resolve = &resolve;
  plugin->scroll_down = &scrollDown;
  plugin->query_scroll_state = &queryScrollState;
  plugin->close_session = &closeSession;
  plugin->shutdown = &shutdown;
  return QINGYING_LONGSHOT_STATUS_OK;
}
