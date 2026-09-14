#include "uia_region_query_worker.hpp"

#include <algorithm>
#include <condition_variable>
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

void runProductionQuery(const UiaRegionQueryRequest& request,
                        UiaRegionQueryResult& result,
                        UiaRegionLocatorSession& session) noexcept
{
  result.request_id = request.request_id;
  result.root_window = request.root_window;
  result.screen_point = request.screen_point;
  result.owner_rect = request.owner_rect;

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
      result.candidate_count < SmartRegionMaxAccessibilityCandidates)
  {
    result.msaa_attempted = true;
    SmartRegionCandidate msaa_candidate;
    SmartRegionMsaaTraversalDiagnostic* const msaa_diagnostic =
        request.diagnostics_enabled ? &result.msaa_diagnostic : nullptr;
    if (locateMsaaCandidate(request.root_window, request.screen_point,
                            msaa_candidate, msaa_diagnostic))
    {
      result.candidates[result.candidate_count++] = msaa_candidate;
    }
  }
  result.succeeded = result.candidate_count != 0;
}

}  // namespace

bool isUiaQueryResultApplicable(
    const UiaRegionQueryResult& result,
    const UiaRegionQueryRequest& current_request,
    std::uint64_t now_ms) noexcept
{
  if (result.root_window == nullptr ||
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
                                 current_request.owner_rect, selected);
}

struct UiaRegionQueryWorker::Impl
{
  struct StoredRequest
  {
    UiaRegionQueryRequest value;
    std::uint64_t generation{0};
  };

  explicit Impl(UiaRegionQueryFunction function, void* context) noexcept
      : query_function(function), query_context(context)
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
        request = pending_request;
        has_request = false;
        query_active = true;
        active_request_id = request.value.request_id;
        const bool cache_is_fresh =
            has_cached_result &&
            request.value.requested_at_ms >= cached_at_ms &&
            request.value.requested_at_ms - cached_at_ms <=
                kPositionCacheLifetimeMs &&
            isUiaQueryResultApplicable(cached_result, request.value,
                                       request.value.requested_at_ms);
        if (cache_is_fresh)
        {
          use_cached_result = true;
        }
        const bool cooldown_is_active =
            has_failure && request.value.root_window == failure_window &&
            request.value.requested_at_ms >= failure_at_ms &&
            request.value.requested_at_ms - failure_at_ms <
                kFailureCooldownMs &&
            pointsAreNear(request.value.screen_point, failure_point);
        if (!use_cached_result && cooldown_is_active)
        {
          suppress_for_cooldown = true;
        }
      }

      UiaRegionQueryResult query_result;
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
      else
      {
        if (query_function != nullptr)
        {
          query_function(request.value, query_result, query_context);
        }
        else if (apartment.usable())
        {
          runProductionQuery(request.value, query_result, session);
        }
      }
      query_result.request_id = request.value.request_id;
      query_result.root_window = request.value.root_window;
      query_result.screen_point = request.value.screen_point;
      query_result.owner_rect = request.value.owner_rect;
      query_result.requested_at_ms = request.value.requested_at_ms;
      query_result.elapsed_ms = GetTickCount64() - begin_ms;

      {
        std::lock_guard<std::mutex> lock(mutex);
        query_active = false;
        last_completed_request_id = request.value.request_id;
        active_request_id = 0;
        const bool newer_request_waiting =
            has_request && pending_request.value.request_id >
                               request.value.request_id;
        const bool completed_result_applies_to_pending_request =
            newer_request_waiting &&
            isUiaQueryResultApplicable(query_result, pending_request.value,
                                       GetTickCount64());
        if (!stop_requested && request.generation == generation)
        {
          if (!use_cached_result && !suppress_for_cooldown)
          {
            if (query_result.succeeded)
            {
              cached_result = query_result;
              cached_at_ms = request.value.requested_at_ms;
              has_cached_result = true;
              has_failure = false;
            }
            else
            {
              failure_window = request.value.root_window;
              failure_point = request.value.screen_point;
              failure_at_ms = request.value.requested_at_ms;
              has_failure = true;
            }
          }
          if (!newer_request_waiting ||
              completed_result_applies_to_pending_request)
          {
            latest_result = query_result;
            has_result = true;
          }
        }
      }
    }
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
  std::uint64_t last_completed_request_id{0};
  HWND failure_window{nullptr};
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
    UiaRegionQueryFunction query_function, void* query_context)
    : m_impl(std::make_unique<Impl>(query_function, query_context))
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
  std::size_t candidate_count = retainAccessibilityCandidates(
      result.candidates, result.candidate_count, candidates,
      std::size(candidates));
  if (fast_candidate.valid() && candidate_count < SmartRegionMaxCandidates)
  {
    candidates[candidate_count++] = fast_candidate;
  }
  return SmartRegionCandidateSelector::selectBest(
      candidates, candidate_count, screen_point.x, screen_point.y,
      owner_rect, out);
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
        !isUiaQueryResultApplicable(m_impl->latest_result, request_value,
                                    GetTickCount64()))
    {
      m_impl->has_result = false;
    }
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
  m_impl->has_result = false;
  m_impl->has_cached_result = false;
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
    if (!m_impl->done_state &&
        m_impl->done_condition.wait_until(lock, deadline) ==
            std::cv_status::timeout &&
        !m_impl->done_state)
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
  return "thread=uia_query_worker request_id=" +
         std::to_string(request_id) + " plugin_id=windows_uia queue_length=" +
         std::to_string(m_impl->has_request ? 1u : 0u) +
         " last_progress=" + progress;
}

void UiaRegionQueryWorker::stop() noexcept
{
  beginStop();
  (void)joinUntil((std::chrono::steady_clock::time_point::max)());
}

}  // namespace qingying::window_detail
