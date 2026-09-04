#include "builtin_longshot_plugin_support.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>

namespace qingying::builtin_longshot_plugin {

namespace {

using RequestApi = QingYingLongShotRequestV1;
using TargetApi = QingYingLongShotTargetV1;
using StateApi = QingYingLongShotScrollStateV1;

bool hasStructSize(std::uint32_t actual, std::size_t required) {
  return actual >= required;
}

bool toCoreRequest(const RequestApi& source, LongShotRequest& target) {
  target = LongShotRequest{};
  if (!hasStructSize(source.struct_size, sizeof(source)) ||
      source.owner_window >
          static_cast<std::uint64_t>(
              (std::numeric_limits<std::uintptr_t>::max)())) {
    return false;
  }

  target.owner_window = static_cast<std::uintptr_t>(source.owner_window);
  target.x = source.x;
  target.y = source.y;
  target.width = source.width;
  target.height = source.height;
  return target.valid();
}

bool toPluginTarget(const LongShotProfileResult& source,
                    const QingYingLongShotSessionV1& session,
                    TargetApi& target) {
  if (!source.valid() ||
      source.scroll_target >
          static_cast<std::uintptr_t>(
              (std::numeric_limits<std::uint64_t>::max)())) {
    return false;
  }

  const std::uint32_t struct_size = target.struct_size;
  target = TargetApi{};
  target.struct_size = struct_size;
  target.flags = QINGYING_LONGSHOT_TARGET_CONTENT_RECT_EXACT;
  target.scroll_target = static_cast<std::uint64_t>(source.scroll_target);
  target.content_x = source.content.x;
  target.content_y = source.content.y;
  target.content_width = source.content.width;
  target.content_height = source.content.height;
  target.opaque_cookie = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(&session));
  return true;
}

bool sameRequest(const LongShotRequest& left,
                 const LongShotRequest& right) {
  return left.owner_window == right.owner_window && left.x == right.x &&
         left.y == right.y && left.width == right.width &&
         left.height == right.height;
}

bool sameTarget(const TargetApi& left, const TargetApi& right) {
  return left.flags == right.flags &&
         left.scroll_target == right.scroll_target &&
         left.content_x == right.content_x &&
         left.content_y == right.content_y &&
         left.content_width == right.content_width &&
         left.content_height == right.content_height &&
         left.opaque_cookie == right.opaque_cookie;
}

bool resolveCoreTarget(const PluginContext& context,
                       const LongShotRequest& request,
                       LongShotProfileResult& target) {
  target = LongShotProfileResult{};
  if (context.resolve == nullptr || !context.resolve(request, target) ||
      !target.valid()) {
    target = LongShotProfileResult{};
    return false;
  }
  return true;
}

QingYingLongShotSessionV1* toSession(void* value) {
  return static_cast<QingYingLongShotSessionV1*>(value);
}

std::int32_t QINGYING_LONGSHOT_PLUGIN_CALL probe(
    void* plugin_context, const RequestApi* request,
    QingYingLongShotProbeResultV1* result) {
  auto* context = static_cast<PluginContext*>(plugin_context);
  if (context == nullptr || request == nullptr || result == nullptr ||
      !hasStructSize(result->struct_size, sizeof(*result))) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  const std::uint32_t struct_size = result->struct_size;
  *result = QingYingLongShotProbeResultV1{};
  result->struct_size = struct_size;

  LongShotRequest core_request;
  if (!toCoreRequest(*request, core_request)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  LongShotProfileResult core_target;
  if (resolveCoreTarget(*context, core_request, core_target)) {
    result->flags = QINGYING_LONGSHOT_PROBE_ACCEPTED;
    result->score = context->priority;
  }
  return QINGYING_LONGSHOT_STATUS_OK;
}

std::int32_t QINGYING_LONGSHOT_PLUGIN_CALL open(
    void* plugin_context, const RequestApi* request,
    QingYingLongShotSessionV1** session) {
  auto* context = static_cast<PluginContext*>(plugin_context);
  if (session != nullptr) {
    *session = nullptr;
  }
  if (context == nullptr || request == nullptr || session == nullptr) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  LongShotRequest core_request;
  if (!toCoreRequest(*request, core_request)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  auto* created = new (std::nothrow) QingYingLongShotSessionV1;
  if (created == nullptr) {
    return QINGYING_LONGSHOT_STATUS_FAILED;
  }
  created->context = context;
  created->request = core_request;
  *session = created;
  return QINGYING_LONGSHOT_STATUS_OK;
}

std::int32_t QINGYING_LONGSHOT_PLUGIN_CALL resolve(
    QingYingLongShotSessionV1* session, const RequestApi* request,
    TargetApi* target) {
  if (session == nullptr || request == nullptr || target == nullptr ||
      !hasStructSize(target->struct_size, sizeof(*target))) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  const std::uint32_t struct_size = target->struct_size;
  *target = TargetApi{};
  target->struct_size = struct_size;

  PluginContext* context = session->context;
  LongShotRequest core_request;
  if (context == nullptr || !toCoreRequest(*request, core_request) ||
      !sameRequest(core_request, session->request)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  LongShotProfileResult core_target;
  if (!resolveCoreTarget(*context, core_request, core_target)) {
    session->has_target = false;
    session->profile = LongShotProfileResult{};
    return QINGYING_LONGSHOT_STATUS_NO_MATCH;
  }

  if (context->host == nullptr || context->host->is_descendant == nullptr ||
      context->host->is_descendant(
          context->host->user_data,
          static_cast<std::uint64_t>(core_request.owner_window),
          static_cast<std::uint64_t>(core_target.scroll_target)) == 0) {
    session->has_target = false;
    session->profile = LongShotProfileResult{};
    return QINGYING_LONGSHOT_STATUS_NOT_SUPPORTED;
  }

  session->profile = core_target;
  if (!toPluginTarget(core_target, *session, *target)) {
    session->has_target = false;
    session->profile = LongShotProfileResult{};
    return QINGYING_LONGSHOT_STATUS_FAILED;
  }
  session->target = *target;
  session->has_target = true;
  return QINGYING_LONGSHOT_STATUS_OK;
}

std::int32_t QINGYING_LONGSHOT_PLUGIN_CALL scrollDown(
    QingYingLongShotSessionV1* session, const RequestApi* request,
    const TargetApi* target) {
  if (session == nullptr || request == nullptr || target == nullptr ||
      !hasStructSize(target->struct_size, sizeof(*target)) ||
      !session->has_target || !sameTarget(*target, session->target)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  PluginContext* context = session->context;
  LongShotRequest core_request;
  if (context == nullptr || !toCoreRequest(*request, core_request) ||
      !sameRequest(core_request, session->request) || context->host == nullptr ||
      context->host->send_wheel_down == nullptr) {
    return QINGYING_LONGSHOT_STATUS_NOT_SUPPORTED;
  }

  return context->host->send_wheel_down(context->host->user_data, request,
                                        target);
}

std::int32_t QINGYING_LONGSHOT_PLUGIN_CALL queryScrollState(
    QingYingLongShotSessionV1* session, const TargetApi* target,
    StateApi* state) {
  if (session == nullptr || target == nullptr || state == nullptr ||
      !hasStructSize(target->struct_size, sizeof(*target)) ||
      !hasStructSize(state->struct_size, sizeof(*state)) ||
      !session->has_target || !sameTarget(*target, session->target)) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  PluginContext* context = session->context;
  if (context == nullptr || context->host == nullptr ||
      context->host->query_scroll_state == nullptr) {
    return QINGYING_LONGSHOT_STATUS_NOT_SUPPORTED;
  }

  return context->host->query_scroll_state(context->host->user_data, target,
                                            state);
}

void QINGYING_LONGSHOT_PLUGIN_CALL closeSession(
    QingYingLongShotSessionV1* session) {
  delete toSession(session);
}

void QINGYING_LONGSHOT_PLUGIN_CALL shutdown(void* plugin_context) {
  auto* context = static_cast<PluginContext*>(plugin_context);
  if (context != nullptr) {
    context->host = nullptr;
    context->resolve = nullptr;
  }
}

}  // namespace

std::int32_t initializePlugin(const QingYingLongShotHostV1* host,
                              QingYingLongShotPluginV1* plugin,
                              PluginContext& context, const char* id_utf8,
                              const char* display_name_utf8,
                              std::int32_t priority,
                              ResolveFunction resolve_function,
                              bool supports_native_scroll_state) {
  if (host == nullptr || plugin == nullptr || id_utf8 == nullptr ||
      id_utf8[0] == '\0' || display_name_utf8 == nullptr ||
      !hasStructSize(host->struct_size, sizeof(*host)) ||
      host->abi_version != QINGYING_LONGSHOT_HOST_ABI_VERSION_V1 ||
      !hasStructSize(plugin->struct_size, sizeof(*plugin)) ||
      resolve_function == nullptr) {
    return QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT;
  }

  const std::uint32_t struct_size = plugin->struct_size;
  *plugin = QingYingLongShotPluginV1{};
  plugin->struct_size = struct_size;

  context.host = host;
  context.resolve = resolve_function;
  context.id_utf8 = id_utf8;
  context.display_name_utf8 = display_name_utf8;
  context.priority = priority;

  plugin->abi_version = QINGYING_LONGSHOT_PLUGIN_ABI_VERSION_V1;
  plugin->priority = priority;
  plugin->capabilities = supports_native_scroll_state
                             ? QINGYING_LONGSHOT_CAP_NATIVE_SCROLL_STATE
                             : 0;
  plugin->id_utf8 = context.id_utf8;
  plugin->display_name_utf8 = context.display_name_utf8;
  plugin->plugin_context = &context;
  plugin->probe = &probe;
  plugin->open = &open;
  plugin->resolve = &resolve;
  plugin->scroll_down = &scrollDown;
  plugin->query_scroll_state =
      supports_native_scroll_state ? &queryScrollState : nullptr;
  plugin->close_session = &closeSession;
  plugin->shutdown = &shutdown;
  return QINGYING_LONGSHOT_STATUS_OK;
}

}  // namespace qingying::builtin_longshot_plugin
