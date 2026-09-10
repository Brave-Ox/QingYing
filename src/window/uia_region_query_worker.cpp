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
      SmartRegionMaxCandidates, uia_candidate_count));
  result.candidate_count = uia_candidate_count;

  const bool has_local_uia =
      SmartRegionCandidateSelector::hasValidLocalCandidate(
          result.candidates, result.candidate_count, request.screen_point.x,
          request.screen_point.y, request.owner_rect,
          SmartRegionDiagnosticSource::Uia);
  if (!has_local_uia &&
      result.candidate_count < SmartRegionMaxCandidates)
  {
    result.msaa_attempted = true;
    SmartRegionCandidate msaa_candidate;
    if (locateMsaaCandidate(request.root_window, request.screen_point,
                            msaa_candidate))
    {
      result.candidates[result.candidate_count++] = msaa_candidate;
    }
  }
  result.succeeded = result.candidate_count != 0;
}

}  // namespace

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
        const bool cache_is_fresh =
            has_cached_result &&
            request.value.root_window == cached_result.root_window &&
            request.value.requested_at_ms >= cached_at_ms &&
            request.value.requested_at_ms - cached_at_ms <=
                kPositionCacheLifetimeMs &&
            pointsAreNear(request.value.screen_point,
                          cached_result.screen_point);
        if (cache_is_fresh)
        {
          use_cached_result = true;
        }
        const bool cooldown_is_active =
            has_failure && request.value.root_window == failure_window &&
            request.value.requested_at_ms >= failure_at_ms &&
            request.value.requested_at_ms - failure_at_ms <
                kFailureCooldownMs;
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
      query_result.elapsed_ms = GetTickCount64() - begin_ms;

      {
        std::lock_guard<std::mutex> lock(mutex);
        query_active = false;
        const bool newer_request_waiting =
            has_request && pending_request.value.request_id >
                               request.value.request_id;
        if (!stop_requested && request.generation == generation &&
            !newer_request_waiting)
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
              failure_at_ms = request.value.requested_at_ms;
              has_failure = true;
            }
          }
          latest_result = query_result;
          has_result = true;
        }
      }
    }
  }

  UiaRegionQueryFunction query_function{nullptr};
  void* query_context{nullptr};
  mutable std::mutex mutex;
  std::condition_variable condition;
  std::thread worker_thread;
  StoredRequest pending_request;
  UiaRegionQueryResult latest_result;
  UiaRegionQueryResult cached_result;
  std::uint64_t generation{0};
  std::uint64_t cached_at_ms{0};
  std::uint64_t failure_at_ms{0};
  HWND failure_window{nullptr};
  bool started{false};
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
                             SmartRegionCandidate& out) noexcept
{
  SmartRegionCandidate candidates[SmartRegionMaxCandidates];
  std::size_t candidate_count = (std::min)(
      result.candidate_count, SmartRegionMaxCandidates);
  for (std::size_t index = 0; index < candidate_count; ++index)
  {
    candidates[index] = result.candidates[index];
  }
  if (fast_candidate.valid() && candidate_count < SmartRegionMaxCandidates)
  {
    candidates[candidate_count++] = fast_candidate;
  }
  return SmartRegionCandidateSelector::selectBest(
      candidates, candidate_count, result.screen_point.x,
      result.screen_point.y, result.owner_rect, out);
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
    m_impl->worker_thread = std::thread([impl = m_impl.get()]() {
      impl->run();
    });
    m_impl->started = true;
  }
  catch (...)
  {
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
    m_impl->has_result = false;
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

void UiaRegionQueryWorker::stop() noexcept
{
  std::thread worker_thread;
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
    worker_thread = std::move(m_impl->worker_thread);
  }
  m_impl->condition.notify_one();
  if (worker_thread.joinable())
  {
    worker_thread.join();
  }
  std::lock_guard<std::mutex> lock(m_impl->mutex);
  m_impl->started = false;
  m_impl->query_active = false;
}

}  // namespace qingying::window_detail
