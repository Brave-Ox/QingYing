#ifndef QINGYING_LONGSHOT_PLUGIN_API_H
#define QINGYING_LONGSHOT_PLUGIN_API_H

// This header is the only contract shared by QingYing and long-shot profile
// DLLs. It intentionally contains C-compatible types only: do not add STL
// types, C++ classes, exceptions, or ownership-transfer APIs here.

#include <stdint.h>

#if defined(_WIN32)
#define QINGYING_LONGSHOT_PLUGIN_CALL __cdecl
#if defined(QINGYING_LONGSHOT_PLUGIN_EXPORTS)
#define QINGYING_LONGSHOT_PLUGIN_EXPORT __declspec(dllexport)
#else
#define QINGYING_LONGSHOT_PLUGIN_EXPORT
#endif
#else
#define QINGYING_LONGSHOT_PLUGIN_CALL
#define QINGYING_LONGSHOT_PLUGIN_EXPORT
#endif

#define QINGYING_LONGSHOT_PLUGIN_ABI_VERSION_V1 1u
#define QINGYING_LONGSHOT_HOST_ABI_VERSION_V1 1u
#define QINGYING_LONGSHOT_PLUGIN_ENTRY_SYMBOL_V1 \
  "qingying_longshot_plugin_entry_v1"

// Callback return values. Callback functions return int32_t instead of bool
// so the representation is explicit in both C and C++.
#define QINGYING_LONGSHOT_STATUS_OK 0
#define QINGYING_LONGSHOT_STATUS_NO_MATCH 1
#define QINGYING_LONGSHOT_STATUS_NOT_SUPPORTED 2
#define QINGYING_LONGSHOT_STATUS_INVALID_ARGUMENT 3
#define QINGYING_LONGSHOT_STATUS_RETRY 4
#define QINGYING_LONGSHOT_STATUS_FAILED 5

// QingYing is currently a 64-bit Windows application. Window handles are
// represented as uint64_t instead of HWND so Windows.h never crosses the ABI.
// A plugin built for another architecture must not be loaded by the host.
typedef struct QingYingLongShotRequestV1 {
  uint32_t struct_size;
  uint32_t reserved0;
  uint64_t owner_window;
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
} QingYingLongShotRequestV1;

// opaque_cookie belongs to the plugin session. The host never dereferences,
// frees, or persists it; it only returns it to callbacks for the same session.
typedef struct QingYingLongShotTargetV1 {
  uint32_t struct_size;
  uint32_t flags;
  uint64_t scroll_target;
  int32_t content_x;
  int32_t content_y;
  int32_t content_width;
  int32_t content_height;
  uint64_t opaque_cookie;
} QingYingLongShotTargetV1;

#define QINGYING_LONGSHOT_TARGET_CONTENT_RECT_EXACT (1u << 0)

typedef struct QingYingLongShotScrollStateV1 {
  uint32_t struct_size;
  uint32_t flags;
  int64_t position;
  int64_t last_position;
} QingYingLongShotScrollStateV1;

#define QINGYING_LONGSHOT_SCROLL_STATE_VALID (1u << 0)
#define QINGYING_LONGSHOT_SCROLL_STATE_AT_BOTTOM (1u << 1)

typedef struct QingYingLongShotProbeResultV1 {
  uint32_t struct_size;
  uint32_t flags;
  int32_t score;
  int32_t reserved0;
} QingYingLongShotProbeResultV1;

#define QINGYING_LONGSHOT_PROBE_ACCEPTED (1u << 0)

typedef struct QingYingLongShotSessionV1 QingYingLongShotSessionV1;

typedef void(QINGYING_LONGSHOT_PLUGIN_CALL* QingYingLongShotLogFnV1)(
    void* user_data, int32_t level, const char* message_utf8);

typedef int32_t(QINGYING_LONGSHOT_PLUGIN_CALL*
                QingYingLongShotIsDescendantFnV1)(
    void* user_data, uint64_t owner_window, uint64_t candidate_window);

typedef int32_t(QINGYING_LONGSHOT_PLUGIN_CALL*
                QingYingLongShotSendWheelDownFnV1)(
    void* user_data, const QingYingLongShotRequestV1* request,
    const QingYingLongShotTargetV1* target);

typedef int32_t(QINGYING_LONGSHOT_PLUGIN_CALL*
                QingYingLongShotQueryScrollStateFnV1)(
    void* user_data, const QingYingLongShotTargetV1* target,
    QingYingLongShotScrollStateV1* state);

// Host callbacks are optional and must be checked for NULL. The host owns
// user_data and all callback arguments. Plugins must not retain any pointer
// received through this table after the callback returns.
typedef struct QingYingLongShotHostV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  void* user_data;
  QingYingLongShotLogFnV1 log;
  QingYingLongShotIsDescendantFnV1 is_descendant;
  QingYingLongShotSendWheelDownFnV1 send_wheel_down;
  QingYingLongShotQueryScrollStateFnV1 query_scroll_state;
} QingYingLongShotHostV1;

typedef int32_t(QINGYING_LONGSHOT_PLUGIN_CALL* QingYingLongShotProbeFnV1)(
    void* plugin_context, const QingYingLongShotRequestV1* request,
    QingYingLongShotProbeResultV1* result);

typedef int32_t(QINGYING_LONGSHOT_PLUGIN_CALL* QingYingLongShotOpenFnV1)(
    void* plugin_context, const QingYingLongShotRequestV1* request,
    QingYingLongShotSessionV1** session);

typedef int32_t(QINGYING_LONGSHOT_PLUGIN_CALL* QingYingLongShotResolveFnV1)(
    QingYingLongShotSessionV1* session,
    const QingYingLongShotRequestV1* request,
    QingYingLongShotTargetV1* target);

typedef int32_t(QINGYING_LONGSHOT_PLUGIN_CALL*
                QingYingLongShotScrollDownFnV1)(
    QingYingLongShotSessionV1* session,
    const QingYingLongShotRequestV1* request,
    const QingYingLongShotTargetV1* target);

typedef int32_t(QINGYING_LONGSHOT_PLUGIN_CALL*
                QingYingLongShotQuerySessionScrollStateFnV1)(
    QingYingLongShotSessionV1* session,
    const QingYingLongShotTargetV1* target,
    QingYingLongShotScrollStateV1* state);

typedef void(QINGYING_LONGSHOT_PLUGIN_CALL* QingYingLongShotCloseFnV1)(
    QingYingLongShotSessionV1* session);

typedef void(QINGYING_LONGSHOT_PLUGIN_CALL* QingYingLongShotShutdownFnV1)(
    void* plugin_context);

// Optional in the tail of the v1 descriptor. The host may call this from the
// cancellation thread while one plugin callback is running. Implementations
// must only signal cancellation and return promptly; they must not free the
// plugin context or unload their module from this callback.
typedef void(QINGYING_LONGSHOT_PLUGIN_CALL* QingYingLongShotCancelFnV1)(
    void* plugin_context);

#define QINGYING_LONGSHOT_CAP_NATIVE_SCROLL_STATE (1u << 0)

// One DLL exposes one profile descriptor. Strings are UTF-8, owned by the
// plugin, and must remain valid until shutdown returns. plugin_context and
// session are also plugin-owned opaque values; the host never calls free().
// probe/open/resolve/scroll_down/close_session are required. The query callback
// is optional unless the native-scroll-state capability is advertised.
typedef struct QingYingLongShotPluginV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  int32_t priority;
  uint32_t capabilities;
  const char* id_utf8;
  const char* display_name_utf8;
  void* plugin_context;
  QingYingLongShotProbeFnV1 probe;
  QingYingLongShotOpenFnV1 open;
  QingYingLongShotResolveFnV1 resolve;
  QingYingLongShotScrollDownFnV1 scroll_down;
  QingYingLongShotQuerySessionScrollStateFnV1 query_scroll_state;
  QingYingLongShotCloseFnV1 close_session;
  QingYingLongShotShutdownFnV1 shutdown;
  // Optional tail field. Older v1 plugins remain valid when struct_size ends
  // at shutdown and therefore expose this as NULL to the host.
  QingYingLongShotCancelFnV1 cancel;
} QingYingLongShotPluginV1;

// The host obtains this symbol with GetProcAddress. The entry point fills the
// caller-provided descriptor instead of returning a pointer whose ownership
// would be ambiguous across the DLL boundary.
typedef int32_t(QINGYING_LONGSHOT_PLUGIN_CALL* QingYingLongShotEntryFnV1)(
    const QingYingLongShotHostV1* host,
    QingYingLongShotPluginV1* plugin);

#ifdef __cplusplus
static_assert(sizeof(QingYingLongShotRequestV1) == 32,
              "QingYingLongShotRequestV1 ABI changed");
static_assert(sizeof(QingYingLongShotTargetV1) == 40,
              "QingYingLongShotTargetV1 ABI changed");
static_assert(sizeof(QingYingLongShotScrollStateV1) == 24,
              "QingYingLongShotScrollStateV1 ABI changed");
static_assert(sizeof(QingYingLongShotProbeResultV1) == 16,
              "QingYingLongShotProbeResultV1 ABI changed");
#endif

#ifdef __cplusplus
extern "C" {
#endif

QINGYING_LONGSHOT_PLUGIN_EXPORT int32_t
QINGYING_LONGSHOT_PLUGIN_CALL qingying_longshot_plugin_entry_v1(
    const QingYingLongShotHostV1* host,
    QingYingLongShotPluginV1* plugin);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // QINGYING_LONGSHOT_PLUGIN_API_H
