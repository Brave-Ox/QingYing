#include "qingying/longshot/dll_longshot_profile.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>

namespace qingying {

namespace {

using PluginApi = QingYingLongShotPluginV1;
using RequestApi = QingYingLongShotRequestV1;
using ProbeApi = QingYingLongShotProbeResultV1;
using TargetApi = QingYingLongShotTargetV1;
using StateApi = QingYingLongShotScrollStateV1;
using SessionApi = QingYingLongShotSessionV1;

struct RequestKey {
  std::uintptr_t owner_window{0};
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  bool operator<(const RequestKey& other) const noexcept {
    return std::tie(owner_window, x, y, width, height) <
           std::tie(other.owner_window, other.x, other.y, other.width,
                    other.height);
  }
};

RequestKey makeRequestKey(const LongShotRequest& request) {
  return {request.owner_window, request.x, request.y, request.width,
          request.height};
}

bool toPluginRequest(const LongShotRequest& source, RequestApi& target) {
  target = RequestApi{};
  if (!source.valid() ||
      source.owner_window >
          static_cast<std::uintptr_t>((std::numeric_limits<std::uint64_t>::
                                           max)())) {
    return false;
  }

  target.struct_size = sizeof(target);
  target.owner_window = static_cast<std::uint64_t>(source.owner_window);
  target.x = source.x;
  target.y = source.y;
  target.width = source.width;
  target.height = source.height;
  return true;
}

bool toCoreTarget(const TargetApi& source, LongShotProfileResult& target) {
  target = LongShotProfileResult{};
  if (source.struct_size < sizeof(source) ||
      source.scroll_target >
          static_cast<std::uint64_t>(
              (std::numeric_limits<std::uintptr_t>::max)())) {
    return false;
  }

  target.scroll_target = static_cast<std::uintptr_t>(source.scroll_target);
  target.content = ScreenPhysicalRect{source.content_x, source.content_y,
                                      source.content_width,
                                      source.content_height};
  return target.valid();
}

bool sameProfileResult(const LongShotProfileResult& left,
                       const LongShotProfileResult& right) {
  return left.scroll_target == right.scroll_target &&
         left.content.x == right.content.x &&
         left.content.y == right.content.y &&
         left.content.width == right.content.width &&
         left.content.height == right.content.height;
}

void closePluginSession(const LongShotPluginHost::LoadedPlugin& loaded, SessionApi*& session) noexcept {
  const auto& plugin = loaded.api();
  if (session != nullptr) {
    auto* closing = session;
    session = nullptr;
    if (plugin.close_session != nullptr) {
      loaded.invoke([&] { plugin.close_session(closing); return QINGYING_LONGSHOT_STATUS_OK; }, true);
    }
  }
}

}  // namespace

struct DllLongShotProfile::Impl {
  struct SessionState {
    SessionApi* session{nullptr};
    TargetApi target{};
    LongShotProfileResult profile{};
  };

  using SessionMap = std::map<RequestKey, SessionState>;

  explicit Impl(const LongShotPluginHost::LoadedPlugin& loaded_plugin)
      : plugin(&loaded_plugin),
        name(loaded_plugin.api().id_utf8 == nullptr
                 ? ""
                 : loaded_plugin.api().id_utf8) {}

  ~Impl() noexcept {
    for (auto& entry : sessions) {
      closePluginSession(*plugin, entry.second.session);
    }
  }

  SessionState* findByProfile(const LongShotProfileResult& profile) {
    for (auto& entry : sessions) {
      if (sameProfileResult(entry.second.profile, profile)) {
        return &entry.second;
      }
    }
    return nullptr;
  }

  const LongShotPluginHost::LoadedPlugin* plugin{nullptr};
  const std::string name;
  mutable std::mutex mutex;
  SessionMap sessions;
};

DllLongShotProfile::DllLongShotProfile(
    const LongShotPluginHost::LoadedPlugin& plugin)
    : impl_(std::make_unique<Impl>(plugin)) {}

DllLongShotProfile::~DllLongShotProfile() = default;

const char* DllLongShotProfile::name() const noexcept {
  return impl_->name.c_str();
}

bool DllLongShotProfile::resolve(const LongShotRequest& request,
                                 LongShotProfileResult& out) const {
  out = LongShotProfileResult{};

  RequestApi plugin_request;
  if (!toPluginRequest(request, plugin_request)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  const RequestKey key = makeRequestKey(request);
  const PluginApi& plugin = impl_->plugin->api();
  if (plugin.probe == nullptr || plugin.open == nullptr ||
      plugin.resolve == nullptr) {
    return false;
  }

  ProbeApi probe{};
  probe.struct_size = sizeof(probe);
  const std::int32_t probe_status =
      impl_->plugin->invoke([&] { return plugin.probe(plugin.plugin_context, &plugin_request, &probe); });
  if (probe_status != QINGYING_LONGSHOT_STATUS_OK ||
      probe.struct_size < sizeof(probe) ||
      (probe.flags & QINGYING_LONGSHOT_PROBE_ACCEPTED) == 0) {
    auto existing = impl_->sessions.find(key);
    if (existing != impl_->sessions.end()) {
      closePluginSession(*impl_->plugin, existing->second.session);
      impl_->sessions.erase(existing);
    }
    return false;
  }

  auto [session_it, inserted] = impl_->sessions.try_emplace(key);
  if (inserted) {
    SessionApi* session = nullptr;
    const std::int32_t open_status =
        impl_->plugin->invoke([&] { return plugin.open(plugin.plugin_context, &plugin_request, &session); });
    if (open_status != QINGYING_LONGSHOT_STATUS_OK || session == nullptr) {
      closePluginSession(*impl_->plugin, session);
      impl_->sessions.erase(session_it);
      return false;
    }
    session_it->second.session = session;
  }

  TargetApi plugin_target{};
  plugin_target.struct_size = sizeof(plugin_target);
  const std::int32_t resolve_status =
      impl_->plugin->invoke([&] { return plugin.resolve(session_it->second.session, &plugin_request, &plugin_target); });
  LongShotProfileResult core_target;
  if (resolve_status != QINGYING_LONGSHOT_STATUS_OK ||
      !toCoreTarget(plugin_target, core_target) ||
      !impl_->plugin->isDescendant(plugin_request.owner_window,
                                   plugin_target.scroll_target)) {
    closePluginSession(*impl_->plugin, session_it->second.session);
    impl_->sessions.erase(session_it);
    return false;
  }

  session_it->second.target = plugin_target;
  session_it->second.profile = core_target;
  out = core_target;
  return true;
}

bool DllLongShotProfile::scrollDown(
    const LongShotRequest& request,
    const LongShotProfileResult& profile) const {
  RequestApi plugin_request;
  if (!toPluginRequest(request, plugin_request)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  const PluginApi& plugin = impl_->plugin->api();
  const auto session_it = impl_->sessions.find(makeRequestKey(request));
  if (session_it == impl_->sessions.end() ||
      session_it->second.session == nullptr ||
      !sameProfileResult(session_it->second.profile, profile) ||
      !impl_->plugin->isDescendant(plugin_request.owner_window,
                                   session_it->second.target.scroll_target) ||
      plugin.scroll_down == nullptr) {
    return false;
  }

  return impl_->plugin->invoke([&] { return plugin.scroll_down(session_it->second.session, &plugin_request,
                            &session_it->second.target); }) ==
         QINGYING_LONGSHOT_STATUS_OK;
}

bool DllLongShotProfile::queryScrollState(
    const LongShotProfileResult& profile, LongShotScrollState& out) const {
  out = LongShotScrollState{};

  std::lock_guard<std::mutex> lock(impl_->mutex);
  const PluginApi& plugin = impl_->plugin->api();
  Impl::SessionState* session = impl_->findByProfile(profile);
  if (session == nullptr || session->session == nullptr ||
      plugin.query_scroll_state == nullptr) {
    return false;
  }

  StateApi state{};
  state.struct_size = sizeof(state);
  const std::int32_t status = impl_->plugin->invoke([&] { return plugin.query_scroll_state(
      session->session, &session->target, &state); });
  if (status != QINGYING_LONGSHOT_STATUS_OK ||
      state.struct_size < sizeof(state) ||
      (state.flags & QINGYING_LONGSHOT_SCROLL_STATE_VALID) == 0) {
    return false;
  }

  out.position = state.position;
  out.last_position = state.last_position;
  if ((state.flags & QINGYING_LONGSHOT_SCROLL_STATE_AT_BOTTOM) != 0 &&
      out.position < out.last_position) {
    out.last_position = out.position;
  }
  out.valid = true;
  return true;
}

void DllLongShotProfile::cancel() const noexcept {
  const PluginApi& plugin = impl_->plugin->api();
  constexpr std::size_t kCancelFieldEnd =
      offsetof(PluginApi, cancel) + sizeof(QingYingLongShotCancelFnV1);
  if (plugin.struct_size < kCancelFieldEnd || plugin.cancel == nullptr) {
    return;
  }
  impl_->plugin->invoke([&] { plugin.cancel(plugin.plugin_context); return QINGYING_LONGSHOT_STATUS_OK; }, true);
}

std::size_t addDllLongShotProfiles(const LongShotPluginHost& host,
                                   LongShotProfileRegistry& registry) {
  std::size_t added = 0;
  for (const auto& loaded : host.plugins()) {
    if (loaded == nullptr) {
      continue;
    }
    registry.add(std::make_unique<DllLongShotProfile>(*loaded));
    ++added;
  }
  return added;
}

}  // namespace qingying
