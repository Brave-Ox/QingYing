#include "qingying/window/smart_region_detector.hpp"
#include "qingying/window/smart_region_diagnostics.hpp"
#include "qingying/diagnostics/fault_boundary.h"
#include "smart_region_visual_cache.hpp"
#include "browser_shell_types.hpp"
#include "uia_region_query_worker.hpp"
#include "qingying/app/app_messages.hpp"

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

#include "msaa_region_locator.hpp"
#include "uia_region_locator.hpp"

namespace qingying::window_detail {
namespace {

constexpr int kPositionCacheRadiusPx = 4;
constexpr std::uint64_t kPositionCacheLifetimeMs = 80;
constexpr std::uint64_t kFailureCooldownMs = 100;
constexpr std::uint64_t kBrowserSemanticMissCooldownMs = 16;
constexpr std::uint64_t kReusableResultMaximumAgeMs = 120;

class ScopedMtaApartment
{
 public:
  ScopedMtaApartment() noexcept
      : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)),
        m_should_uninitialize(SUCCEEDED(m_result))
  {
  }

  ~ScopedMtaApartment()
  {
    if (m_should_uninitialize)
    {
      CoUninitialize();
    }
  }

  ScopedMtaApartment(const ScopedMtaApartment&) = delete;
  ScopedMtaApartment& operator=(const ScopedMtaApartment&) = delete;

  bool usable() const noexcept
  {
    return SUCCEEDED(m_result);
  }

 private:
  HRESULT m_result{E_FAIL};
  bool m_should_uninitialize{false};
};

bool pointsAreNear(POINT left, POINT right) noexcept
{
  const std::int64_t delta_x =
      static_cast<std::int64_t>(left.x) - right.x;
  const std::int64_t delta_y =
      static_cast<std::int64_t>(left.y) - right.y;
  return delta_x >= -kPositionCacheRadiusPx &&
         delta_x <= kPositionCacheRadiusPx &&
         delta_y >= -kPositionCacheRadiusPx &&
         delta_y <= kPositionCacheRadiusPx;
}

bool rectanglesAreEqual(const WindowRect& left,
                        const WindowRect& right) noexcept
{
  return left.left == right.left && left.top == right.top &&
         left.right == right.right && left.bottom == right.bottom;
}

bool hasApplicableBrowserShellAtlas(
    const UiaRegionQueryResult& result,
    const UiaRegionQueryRequest& request) noexcept
{
  const std::shared_ptr<const BrowserShellAtlasSnapshot>& atlas =
      result.m_browser_shell_atlas;
  if (atlas == nullptr || atlas->m_entry_count == 0)
  {
    return false;
  }

  const BrowserShellContext& context = atlas->m_context;
  return context.m_root_window == request.root_window &&
         context.m_process_id == request.process_id &&
         rectanglesAreEqual(context.m_window_rect, request.owner_rect) &&
         context.m_capture_session_generation == request.generation &&
         context.m_window_generation == result.m_window_generation &&
         result.m_window_generation == request.generation;
}

void runProductionQuery(const UiaRegionQueryRequest& request,
                        UiaRegionQueryResult& result,
                        UiaRegionLocatorSession& session,
                        SmartRegionVisualResultCache& visual_cache,
                        RegionQueryLane lane,
                        const std::function<void(const UiaRegionQueryResult&)>& progress)
{
  result.request_id = request.request_id;
  result.root_window = request.root_window;
  result.screen_point = request.screen_point;
  result.owner_rect = request.owner_rect;

  if (lane != RegionQueryLane::Accessibility) {
    SmartRegionDetector detector;
    SmartRegionCandidate fallback;
    SmartRegionCandidateCollection discovered;
    SmartRegionWindowSnapshot snapshot;
    const SmartRegionVisualContext visual{request.background.get(), request.image_screen_rect};
    detector.detectAt(request.screen_point.x, request.screen_point.y, fallback,
                      nullptr, &visual, SmartRegionDetectionPolicy::FastFallbackOnly,
                      &discovered, &snapshot, &visual_cache);
    if (snapshot.root_window == reinterpret_cast<std::uintptr_t>(request.root_window)) {
      result.is_chromium_browser_chrome = snapshot.is_chromium_browser_chrome;
      result.minimum_visual_confidence = snapshot.minimum_visual_confidence;
      for (std::size_t i = 0; i < discovered.count() &&
           result.candidate_count < SmartRegionMaxCandidates; ++i)
        result.candidates[result.candidate_count++] = discovered.candidateAt(i);
    }
    result.succeeded = result.candidate_count != 0;
    if (lane == RegionQueryLane::Combined) progress(result);
  }
  if (lane == RegionQueryLane::Discovery) return;
  const auto content_result = result;
  result.candidate_count = 0;
  if (request.deadline_ms && GetTickCount64() >= request.deadline_ms) return;

  std::size_t uia_candidate_count = 0;
  static_cast<void>(session.locate(
      request.root_window, request.screen_point, result.candidates,
      SmartRegionMaxUiaCandidates, uia_candidate_count));
  result.candidate_count = uia_candidate_count;

  const bool has_local_uia =
      SmartRegionCandidateSelector::hasValidLocalCandidate(
          result.candidates, result.candidate_count, request.screen_point.x,
          request.screen_point.y, request.owner_rect,
          SmartRegionDiagnosticSource::Uia);
  if (!has_local_uia &&
      (!request.deadline_ms || GetTickCount64() < request.deadline_ms) &&
      result.candidate_count < SmartRegionMaxAccessibilityCandidates)
  {
    result.msaa_attempted = true;
    SmartRegionCandidate msaa_candidate;
    bool browser_semantic_miss = false;
    SmartRegionMsaaTraversalDiagnostic* const msaa_diagnostic =
        request.diagnostics_enabled ? &result.msaa_diagnostic : nullptr;
    if (locateMsaaCandidate(request.root_window, request.screen_point,
                            msaa_candidate, msaa_diagnostic,
                            &browser_semantic_miss))
    {
      result.candidates[result.candidate_count++] = msaa_candidate;
    }
    result.browser_semantic_miss =
        browser_semantic_miss && result.candidate_count == 0;
  }
  for (std::size_t i = 0; i < content_result.candidate_count &&
       result.candidate_count < SmartRegionMaxCandidates; ++i)
    result.candidates[result.candidate_count++] = content_result.candidates[i];
  result.succeeded = result.candidate_count != 0;
}

}  // namespace

bool isUiaQueryResultApplicable(
    const UiaRegionQueryResult& result,
    const UiaRegionQueryRequest& current_request,
    std::uint64_t now_ms) noexcept
{
  if (result.request_id != current_request.request_id ||
      result.generation != current_request.generation ||
      result.process_id != current_request.process_id || result.timed_out ||
      (result.deadline_ms && now_ms >= result.deadline_ms) ||
      result.root_window == nullptr ||
      result.root_window != current_request.root_window ||
      !rectanglesAreEqual(result.owner_rect, current_request.owner_rect) ||
      current_request.owner_rect.empty() ||
      now_ms < result.requested_at_ms ||
      now_ms - result.requested_at_ms > kReusableResultMaximumAgeMs)
  {
    return false;
  }

  SmartRegionCandidate selected;
  return selectUiaQueryCandidate(result, SmartRegionCandidate{},
                                 current_request.screen_point,
                                 current_request.owner_rect, selected) ||
         hasApplicableBrowserShellAtlas(result, current_request);
}

bool rebindUiaQueryResult(UiaRegionQueryResult& result,
                         const UiaRegionQueryRequest& request,
                         std::uint64_t now_ms) noexcept {
  auto bound = result;
  bound.request_id = request.request_id;
  if (!isUiaQueryResultApplicable(bound, request, now_ms)) return false;
  SmartRegionCandidate local;
  if (!selectUiaQueryCandidate(bound, {}, request.screen_point,
                               request.owner_rect, local))
  {
    if (!hasApplicableBrowserShellAtlas(bound, request))
    {
      return false;
    }
  }
  else if (local.semantic == SmartRegionSemantic::Fallback ||
           local.source == SmartRegionDiagnosticSource::Window ||
           local.source == SmartRegionDiagnosticSource::ClientArea)
  {
    return false;
  }
  bound.screen_point = request.screen_point;
  result = bound;
  return true;
}

namespace {
bool reusableCachedResult(UiaRegionQueryResult result, const UiaRegionQueryRequest& request) noexcept {
  return rebindUiaQueryResult(result, request, request.requested_at_ms);
}
}  // namespace

struct UiaRegionQueryWorker::Impl
{
  struct StoredRequest
  {
    UiaRegionQueryRequest value;
    std::uint64_t generation{0};
  };

  explicit Impl(UiaRegionQueryFunction function, void* context, RegionQueryLane query_lane) noexcept
      : query_function(function), query_context(context), lane(query_lane)
  {
  }

  void run() noexcept
  {
    struct WorkerDone final
    {
      Impl* owner;
      ~WorkerDone() { owner->markDone(); }
    } done{this};
    ScopedMtaApartment apartment;
    UiaRegionLocatorSession session;
    SmartRegionVisualResultCache visual_cache;
    std::uint64_t visual_generation = 0;
    DWORD visual_process_id = 0;
    for (;;)
    {
      StoredRequest request;
      bool use_cached_result = false;
      bool suppress_for_cooldown = false;
      {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [this]() { return stop_requested || has_request; });
        if (stop_requested)
        {
          return;
        }
        request = std::move(pending_request);
        pending_request = {};
        has_request = false;
        query_active = true;
        active_request_id = request.value.request_id;
        active_requested_at_ms = GetTickCount64();
        active_deadline_ms = request.value.deadline_ms;
        const bool cache_is_fresh =
            has_cached_result &&
            request.value.requested_at_ms >= cached_at_ms &&
            request.value.requested_at_ms - cached_at_ms <=
                kPositionCacheLifetimeMs &&
            cached_background == request.value.background &&
            rectanglesAreEqual(cached_image_rect, request.value.image_screen_rect) &&
            reusableCachedResult(cached_result, request.value);
        if (cache_is_fresh)
        {
          use_cached_result = true;
        }
        const bool cooldown_is_active =
            has_failure && request.value.root_window == failure_window &&
            request.value.process_id == failure_process_id &&
            request.value.requested_at_ms >= failure_at_ms &&
            request.value.requested_at_ms - failure_at_ms <
                failure_cooldown_ms &&
            (failure_cooldown_ms >= 1000 ||
             pointsAreNear(request.value.screen_point, failure_point));
        if (!use_cached_result && cooldown_is_active)
        {
          suppress_for_cooldown = true;
        }
      }

      UiaRegionQueryResult query_result;
      bool provider_failed = false;
      const std::uint64_t begin_ms = GetTickCount64();
      if (use_cached_result)
      {
        std::lock_guard<std::mutex> lock(mutex);
        query_result = cached_result;
        query_result.cache_hit = true;
      }
      else if (suppress_for_cooldown)
      {
        query_result.suppressed_by_cooldown = true;
      }
      else if (request.value.deadline_ms && begin_ms >= request.value.deadline_ms)
      {
        query_result.timed_out = true;
      }
      else
      {
        FaultContext context;
        context.session_id = request.value.generation;
        context.request_id = request.value.request_id;
        context.started_at = std::chrono::steady_clock::now();
        DiagnosticScope scope(context);
        const bool provider_ok = containFault(FaultOrigin::Worker, FaultDomain::Provider, [&] {
        if (query_function != nullptr)
        {
          query_function(request.value, query_result, query_context);
        }
        else if (apartment.usable())
        {
          if (visual_generation != request.value.generation || visual_process_id != request.value.process_id) {
            visual_cache.clear();
            visual_generation = request.value.generation;
            visual_process_id = request.value.process_id;
          }
          runProductionQuery(request.value, query_result, session, visual_cache, lane,
              [this, &request](const UiaRegionQueryResult& discovery) {
            auto partial = discovery;
            partial.generation = request.value.generation;
            partial.process_id = request.value.process_id;
            partial.deadline_ms = request.value.deadline_ms;
            partial.requested_at_ms = request.value.requested_at_ms;
            partial.completed_at_ms = GetTickCount64();
            std::lock_guard<std::mutex> lock(mutex);
            if (!stop_requested && request.generation == generation &&
                (!partial.deadline_ms || partial.completed_at_ms < partial.deadline_ms) &&
                (!has_request || (request.value.background == pending_request.value.background &&
                 rectanglesAreEqual(request.value.image_screen_rect, pending_request.value.image_screen_rect) &&
                 rebindUiaQueryResult(partial, pending_request.value, partial.completed_at_ms)))) {
              latest_result = partial;
              latest_background = request.value.background;
              latest_image_rect = request.value.image_screen_rect;
              has_result = true;
              if (request.value.notify_window != nullptr) PostMessageW(request.value.notify_window, WM_QINGYING_SMART_REGION_COMPLETE, 0, 0);
            }
          });
        }
        }, nullptr, laneName());
        if (!provider_ok) {
          provider_failed = true;
          query_result = UiaRegionQueryResult{};
          query_result.succeeded = false;
        }
      }
      query_result.generation = request.value.generation;
      query_result.process_id = request.value.process_id;
      query_result.m_window_generation = request.value.generation;
      query_result.m_pointer_sequence = request.value.request_id;
      if (!use_cached_result) query_result.deadline_ms = request.value.deadline_ms;
      query_result.request_id = request.value.request_id;
      query_result.root_window = request.value.root_window;
      query_result.screen_point = request.value.screen_point;
      query_result.owner_rect = request.value.owner_rect;
      if (!use_cached_result) query_result.requested_at_ms = request.value.requested_at_ms;
      query_result.completed_at_ms = GetTickCount64();
      query_result.elapsed_ms = query_result.completed_at_ms - begin_ms;
      query_result.timed_out = query_result.timed_out ||
          (query_result.deadline_ms && query_result.completed_at_ms >= query_result.deadline_ms);
      if (query_result.timed_out) query_result.succeeded = false;
      if (query_result.timed_out && !suppress_for_cooldown) {
        FaultContext context; context.session_id = request.value.generation;
        context.request_id = request.value.request_id;
        context.started_at = std::chrono::steady_clock::now() - std::chrono::milliseconds(query_result.elapsed_ms);
        recordFault(ErrorCode::kTimeout, FaultOrigin::Worker, FaultDomain::Provider, laneName(), context);
      }

      {
        std::lock_guard<std::mutex> lock(mutex);
        query_active = false;
        last_completed_request_id = request.value.request_id;
        active_request_id = 0;
        const bool newer_request_waiting =
            has_request && pending_request.value.request_id >
                               request.value.request_id;
        auto deliverable_result = query_result;
        const bool completed_result_applies_to_pending_request =
            newer_request_waiting &&
            request.value.background == pending_request.value.background &&
            rectanglesAreEqual(request.value.image_screen_rect, pending_request.value.image_screen_rect) &&
            rebindUiaQueryResult(deliverable_result, pending_request.value, GetTickCount64());
        if (!stop_requested && request.generation == generation)
        {
          if (!use_cached_result && !suppress_for_cooldown)
          {
            if (query_result.succeeded)
            {
              cached_result = query_result;
              cached_background = request.value.background;
              cached_image_rect = request.value.image_screen_rect;
              cached_at_ms = request.value.requested_at_ms;
              has_cached_result = true;
              has_failure = false;
            }
            else
            {
              failure_window = request.value.root_window;
              failure_process_id = request.value.process_id;
              failure_point = request.value.screen_point;
              failure_at_ms = query_result.timed_out ? query_result.completed_at_ms : request.value.requested_at_ms;
              failure_cooldown_ms = (query_result.timed_out || provider_failed) ? 2000 :
                                   query_result.browser_semantic_miss
                                        ? kBrowserSemanticMissCooldownMs
                                        : kFailureCooldownMs;
              has_failure = true;
            }
          }
          if (!newer_request_waiting ||
              completed_result_applies_to_pending_request)
          {
            latest_result = deliverable_result;
            latest_background = request.value.background;
            latest_image_rect = request.value.image_screen_rect;
            has_result = true;
            if (request.value.notify_window != nullptr) PostMessageW(request.value.notify_window, WM_QINGYING_SMART_REGION_COMPLETE, 0, 0);
          }
        }
      }
    }
  }

  const char* laneName() const noexcept {
    return lane == RegionQueryLane::Discovery ? "thread=region_discovery_worker"
                                               : "thread=uia_query_worker";
  }

  void markDone() noexcept
  {
    {
      std::lock_guard<std::mutex> lock(mutex);
      done_state = true;
    }
    done_condition.notify_all();
  }

  UiaRegionQueryFunction query_function{nullptr};
  void* query_context{nullptr};
  RegionQueryLane lane{RegionQueryLane::Combined};
  std::shared_ptr<const Image> cached_background;
  std::shared_ptr<const Image> latest_background;
  WindowRect latest_image_rect;
  WindowRect cached_image_rect;
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::condition_variable done_condition;
  std::thread worker_thread;
  StoredRequest pending_request;
  UiaRegionQueryResult latest_result;
  UiaRegionQueryResult cached_result;
  std::uint64_t generation{0};
  std::uint64_t cached_at_ms{0};
  std::uint64_t failure_at_ms{0};
  std::uint64_t active_request_id{0};
  std::uint64_t active_requested_at_ms{0};
  std::uint64_t active_deadline_ms{0};
  std::uint64_t last_completed_request_id{0};
  std::uint64_t failure_cooldown_ms{kFailureCooldownMs};
  HWND failure_window{nullptr};
  DWORD failure_process_id{0};
  POINT failure_point{};
  bool started{false};
  bool done_state{true};
  bool stop_requested{false};
  bool has_request{false};
  bool query_active{false};
  bool has_result{false};
  bool has_cached_result{false};
  bool has_failure{false};
};

UiaRegionQueryWorker::UiaRegionQueryWorker(
    UiaRegionQueryFunction query_function, void* query_context, RegionQueryLane lane)
    : m_impl(std::make_unique<Impl>(query_function, query_context, lane))
{
}

UiaRegionQueryWorker::~UiaRegionQueryWorker()
{
  stop();
}

bool selectUiaQueryCandidate(const UiaRegionQueryResult& result,
                             const SmartRegionCandidate& fast_candidate,
                             POINT screen_point,
                             const WindowRect& owner_rect,
                             SmartRegionCandidate& out) noexcept
{
  SmartRegionCandidate candidates[SmartRegionMaxCandidates];
  std::size_t candidate_count = retainRegionCandidates(result.candidates, result.candidate_count,
      candidates, SmartRegionMaxCandidates - (fast_candidate.valid() ? 1 : 0));
  if (fast_candidate.valid() && candidate_count < SmartRegionMaxCandidates)
  {
    candidates[candidate_count++] = fast_candidate;
  }
  return SmartRegionCandidateSelector::selectBest(
      candidates, candidate_count, screen_point.x, screen_point.y,
      owner_rect, out, result.minimum_visual_confidence);
}

std::size_t retainRegionCandidates(const SmartRegionCandidate* candidates,
    std::size_t count, SmartRegionCandidate* output, std::size_t capacity) noexcept {
  if (!candidates || !output || !capacity) return 0;
  auto retained = retainAccessibilityCandidates(candidates, count, output, capacity);
  capacity = (std::min)(capacity, SmartRegionMaxCandidates);
  for (std::size_t i = 0; i < (std::min)(count, SmartRegionMaxCandidates) && retained < capacity; ++i)
    if (candidates[i].source != SmartRegionDiagnosticSource::Uia &&
        candidates[i].source != SmartRegionDiagnosticSource::Msaa)
      output[retained++] = candidates[i];
  return retained;
}

std::size_t retainAccessibilityCandidates(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    SmartRegionCandidate* out_candidates, std::size_t capacity) noexcept
{
  if (candidates == nullptr || out_candidates == nullptr || capacity == 0)
  {
    return 0;
  }

  const std::size_t input_count =
      (std::min)(candidate_count, SmartRegionMaxCandidates);
  const std::size_t output_capacity =
      (std::min)(capacity, SmartRegionMaxAccessibilityCandidates);
  std::size_t retained_count = 0;
  std::size_t uia_count = 0;
  for (std::size_t index = 0; index < input_count; ++index)
  {
    const SmartRegionCandidate& candidate = candidates[index];
    if (candidate.source != SmartRegionDiagnosticSource::Uia &&
        candidate.source != SmartRegionDiagnosticSource::Msaa) continue;
    if (candidate.source == SmartRegionDiagnosticSource::Uia)
    {
      if (uia_count >= SmartRegionMaxUiaCandidates)
      {
        continue;
      }
      ++uia_count;
    }
    if (retained_count >= output_capacity)
    {
      break;
    }
    out_candidates[retained_count++] = candidate;
  }
  return retained_count;
}

bool UiaRegionQueryWorker::start() noexcept
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  if (m_impl->started)
  {
    return true;
  }
  if (m_impl->stop_requested)
  {
    return false;
  }
  try
  {
    m_impl->done_state = false;
    m_impl->worker_thread = std::thread([impl = m_impl.get()]() {
      impl->run();
    });
    m_impl->started = true;
  }
  catch (...)
  {
    m_impl->done_state = true;
    return false;
  }
  return true;
}

bool UiaRegionQueryWorker::request(
    const UiaRegionQueryRequest& request_value) noexcept
{
  if (request_value.request_id == 0 || request_value.root_window == nullptr ||
      request_value.owner_rect.empty())
  {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->started || m_impl->stop_requested)
    {
      return false;
    }
    m_impl->pending_request = {request_value, m_impl->generation};
    m_impl->has_request = true;
    if (m_impl->has_result &&
        (m_impl->latest_background != request_value.background ||
         !rectanglesAreEqual(m_impl->latest_image_rect, request_value.image_screen_rect) ||
         !rebindUiaQueryResult(m_impl->latest_result, request_value, GetTickCount64())))
    {
      m_impl->has_result = false;
    }
    if (!m_impl->has_result && m_impl->has_cached_result &&
        m_impl->cached_background == request_value.background &&
        rectanglesAreEqual(m_impl->cached_image_rect, request_value.image_screen_rect) &&
        request_value.requested_at_ms >= m_impl->cached_at_ms &&
        request_value.requested_at_ms - m_impl->cached_at_ms <= kPositionCacheLifetimeMs) {
      auto cached = m_impl->cached_result;
      if (rebindUiaQueryResult(cached, request_value, GetTickCount64())) {
        cached.cache_hit = true;
        m_impl->latest_result = cached;
        m_impl->latest_background = request_value.background;
        m_impl->latest_image_rect = request_value.image_screen_rect;
        m_impl->has_result = true;
      }
    }
    if (m_impl->has_result && request_value.notify_window != nullptr)
      PostMessageW(request_value.notify_window, WM_QINGYING_SMART_REGION_COMPLETE, 0, 0);
  }
  m_impl->condition.notify_one();
  return true;
}

bool UiaRegionQueryWorker::tryTakeLatest(
    UiaRegionQueryResult& out) noexcept
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  if (!m_impl->has_result)
  {
    return false;
  }
  out = m_impl->latest_result;
  m_impl->has_result = false;
  return true;
}

bool UiaRegionQueryWorker::hasPendingWork() const noexcept
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  return m_impl->has_request || m_impl->query_active;
}

void UiaRegionQueryWorker::clear() noexcept
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  ++m_impl->generation;
  m_impl->has_request = false;
  m_impl->pending_request = {};
  m_impl->has_result = false;
  m_impl->has_cached_result = false;
  m_impl->cached_background.reset();
  m_impl->latest_background.reset();
  m_impl->has_failure = false;
}

void UiaRegionQueryWorker::beginStop() noexcept
{
  {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (!m_impl->started)
    {
      return;
    }
    m_impl->stop_requested = true;
    ++m_impl->generation;
    m_impl->has_request = false;
    m_impl->pending_request = {};
    m_impl->has_result = false;
  }
  m_impl->condition.notify_one();
}

bool UiaRegionQueryWorker::joinUntil(
    std::chrono::steady_clock::time_point deadline) noexcept
{
  {
    std::unique_lock<std::mutex> lock(m_impl->mutex);
    if (!m_impl->started) return true;
    if (!m_impl->done_condition.wait_until(lock, deadline,
                                          [this] { return m_impl->done_state; }))
    {
      return false;
    }
  }
  if (m_impl->worker_thread.joinable())
  {
    if (m_impl->worker_thread.get_id() == std::this_thread::get_id())
    {
      return false;
    }
    m_impl->worker_thread.join();
  }
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->started = false;
  m_impl->query_active = false;
  return true;
}

std::string UiaRegionQueryWorker::diagnosticSnapshot() const
{
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  const std::uint64_t request_id =
      m_impl->query_active
          ? m_impl->active_request_id
          : (m_impl->has_request ? m_impl->pending_request.value.request_id
                                 : m_impl->last_completed_request_id);
  const char* progress = m_impl->query_active
                             ? "provider_callback"
                             : (m_impl->has_request ? "request_queued"
                                                    : "worker_idle");
  return std::string(m_impl->laneName()) + " executor=region request_id=" +
         std::to_string(request_id) + " plugin_id=windows_uia queue_length=" +
         std::to_string(m_impl->has_request ? 1u : 0u) +
         " elapsed_ms=" + std::to_string(m_impl->query_active ? GetTickCount64() - m_impl->active_requested_at_ms : 0) +
         " deadline_exceeded=" + std::to_string(m_impl->query_active && m_impl->active_deadline_ms && GetTickCount64() >= m_impl->active_deadline_ms) +
         " last_progress=" + progress;
}

void UiaRegionQueryWorker::stop() noexcept
{
  beginStop();
  (void)joinUntil((std::chrono::steady_clock::time_point::max)());
}

}  // namespace qingying::window_detail
