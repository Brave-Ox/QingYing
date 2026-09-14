#include "qingying/longshot/longshot_plugin_api.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>

struct QingYingLongShotSessionV1 {
  int marker{0};
};

namespace {

QingYingLongShotSessionV1 g_session;
std::condition_variable g_condition;
std::mutex g_mutex;
bool g_entered = false;
bool g_release = false;

int32_t QINGYING_LONGSHOT_PLUGIN_CALL probe(
    void*, const QingYingLongShotRequestV1* request,
    QingYingLongShotProbeResultV1* result) {
  if (request == nullptr || result == nullptr ||
      request->struct_size < sizeof(*request) ||
      result->struct_size < sizeof(*result)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }
  result->flags = QINGYING_LONGSHOT_PROBE_ACCEPTED;
  result->score = 100;
  return QINGYING_LONGSHOT_STATUS_OK;
}

int32_t QINGYING_LONGSHOT_PLUGIN_CALL open(
    void*, const QingYingLongShotRequestV1* request,
    QingYingLongShotSessionV1** session) {
  if (request == nullptr || session == nullptr ||
      request->struct_size < sizeof(*request)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_entered = false;
    g_release = false;
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
  target->opaque_cookie = 0xBADC0FFEu;
  return QINGYING_LONGSHOT_STATUS_OK;
}

int32_t QINGYING_LONGSHOT_PLUGIN_CALL scrollDown(
    QingYingLongShotSessionV1* session,
    const QingYingLongShotRequestV1* request,
    const QingYingLongShotTargetV1* target) {
  if (session == nullptr || request == nullptr || target == nullptr) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }
  std::unique_lock<std::mutex> lock(g_mutex);
  g_entered = true;
  g_condition.notify_all();
  g_condition.wait(lock, [] { return g_release; });
  return QINGYING_LONGSHOT_STATUS_OK;
}

int32_t QINGYING_LONGSHOT_PLUGIN_CALL queryScrollState(
    QingYingLongShotSessionV1*, const QingYingLongShotTargetV1*,
    QingYingLongShotScrollStateV1*) {
  return QINGYING_LONGSHOT_STATUS_NOT_SUPPORTED;
}

void QINGYING_LONGSHOT_PLUGIN_CALL closeSession(
    QingYingLongShotSessionV1*) {}

void QINGYING_LONGSHOT_PLUGIN_CALL shutdown(void*) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_release = true;
  g_condition.notify_all();
}

}  // namespace

extern "C" QINGYING_LONGSHOT_PLUGIN_EXPORT int32_t
QINGYING_LONGSHOT_PLUGIN_CALL qingying_longshot_plugin_entry_v1(
    const QingYingLongShotHostV1* host,
    QingYingLongShotPluginV1* plugin) {
  constexpr std::uint32_t kLegacyStructSize = static_cast<std::uint32_t>(
      offsetof(QingYingLongShotPluginV1, shutdown) +
      sizeof(QingYingLongShotShutdownFnV1));
  if (host == nullptr || plugin == nullptr ||
      host->struct_size < sizeof(*host) ||
      host->abi_version != QINGYING_LONGSHOT_HOST_ABI_VERSION_V1 ||
      plugin->struct_size < kLegacyStructSize) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  plugin->struct_size = kLegacyStructSize;
  plugin->abi_version = QINGYING_LONGSHOT_PLUGIN_ABI_VERSION_V1;
  plugin->priority = 100;
  plugin->capabilities = 0;
  plugin->id_utf8 = "test.blocking.legacy";
  plugin->display_name_utf8 = "QingYing Blocking Legacy Test";
  plugin->plugin_context = nullptr;
  plugin->probe = &probe;
  plugin->open = &open;
  plugin->resolve = &resolve;
  plugin->scroll_down = &scrollDown;
  plugin->query_scroll_state = &queryScrollState;
  plugin->close_session = &closeSession;
  plugin->shutdown = &shutdown;
  plugin->cancel = nullptr;
  return QINGYING_LONGSHOT_STATUS_OK;
}

extern "C" QINGYING_LONGSHOT_PLUGIN_EXPORT int32_t
QINGYING_LONGSHOT_PLUGIN_CALL qingying_test_blocking_longshot_wait_entered(
    std::uint32_t timeout_ms) {
  std::unique_lock<std::mutex> lock(g_mutex);
  return g_condition.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                              [] { return g_entered; })
             ? 1
             : 0;
}

extern "C" QINGYING_LONGSHOT_PLUGIN_EXPORT void
QINGYING_LONGSHOT_PLUGIN_CALL qingying_test_blocking_longshot_release() {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_release = true;
  g_condition.notify_all();
}
