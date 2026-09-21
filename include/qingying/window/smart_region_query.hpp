#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <string>

#include <Windows.h>

#include "qingying/action/image.hpp"
#include "qingying/window/smart_region_diagnostics.hpp"

namespace qingying {
struct BrowserShellAtlasSnapshot;
}

namespace qingying::window_detail {

struct UiaRegionQueryRequest
{
  std::uint64_t request_id{0};
  HWND root_window{nullptr};
  POINT screen_point{};
  WindowRect owner_rect;
  std::uint64_t requested_at_ms{0};
  bool diagnostics_enabled{false};
  std::uint64_t generation{0};
  DWORD process_id{0};
  std::uint64_t deadline_ms{0};
  std::shared_ptr<const Image> background;
  WindowRect image_screen_rect;
  HWND notify_window{nullptr};
};

struct UiaRegionQueryResult
{
  std::uint64_t request_id{0};
  HWND root_window{nullptr};
  POINT screen_point{};
  WindowRect owner_rect;
  SmartRegionCandidate candidates[SmartRegionMaxCandidates];
  std::size_t candidate_count{0};
  std::uint64_t requested_at_ms{0};
  std::uint64_t elapsed_ms{0};
  std::uint64_t completed_at_ms{0};
  bool succeeded{false};
  bool msaa_attempted{false};
  bool browser_semantic_miss{false};
  bool cache_hit{false};
  bool suppressed_by_cooldown{false};
  SmartRegionMsaaTraversalDiagnostic msaa_diagnostic;
  std::uint64_t generation{0};
  DWORD process_id{0};
  std::uint64_t deadline_ms{0};
  bool timed_out{false};
  bool is_chromium_browser_chrome{false};
  std::uint8_t minimum_visual_confidence{70};
  // Immutable background-to-UI handoff.  The public contract keeps the
  // Atlas type incomplete so consumers do not depend on smart-region internals.
  std::shared_ptr<const BrowserShellAtlasSnapshot> m_browser_shell_atlas;
  std::uint64_t m_window_generation{0};
  std::uint64_t m_layout_generation{0};
  std::uint64_t m_pointer_sequence{0};
};

enum class RegionQueryLane { Combined, Discovery, Accessibility };

using UiaRegionQueryFunction = void (*)(
    const UiaRegionQueryRequest& request, UiaRegionQueryResult& result,
    void* context);

// Submission is strictly scoped to the current request and session generation.
bool isUiaQueryResultApplicable(
    const UiaRegionQueryResult& result,
    const UiaRegionQueryRequest& current_request,
    std::uint64_t now_ms) noexcept;

// 仅保留固定预算内的 UIA 和 MSAA 候选，避免异步结果挤掉快速路径的回退链。
std::size_t retainAccessibilityCandidates(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    SmartRegionCandidate* out_candidates, std::size_t capacity) noexcept;

std::size_t retainRegionCandidates(const SmartRegionCandidate* candidates,
    std::size_t count, SmartRegionCandidate* output, std::size_t capacity) noexcept;

// Rebind only a fresh local hit inside the same window/session context.
// UI still accepts only the resulting current request ID.
bool rebindUiaQueryResult(UiaRegionQueryResult& result,
                         const UiaRegionQueryRequest& current_request,
                         std::uint64_t now_ms) noexcept;

bool selectUiaQueryCandidate(const UiaRegionQueryResult& result,
                             const SmartRegionCandidate& fast_candidate,
                             POINT screen_point,
                             const WindowRect& owner_rect,
                             SmartRegionCandidate& out) noexcept;

// Each instance owns one serial background lane with latest-wins submission.
// Query function/context are test hooks; the observer context must outlive the worker.
class UiaRegionQueryWorker
{
 public:
  explicit UiaRegionQueryWorker(
      UiaRegionQueryFunction query_function = nullptr,
      void* query_context = nullptr,
      RegionQueryLane lane = RegionQueryLane::Combined);
  ~UiaRegionQueryWorker();

  UiaRegionQueryWorker(const UiaRegionQueryWorker&) = delete;
  UiaRegionQueryWorker& operator=(const UiaRegionQueryWorker&) = delete;

  bool start() noexcept;
  bool request(const UiaRegionQueryRequest& request) noexcept;
  bool tryTakeLatest(UiaRegionQueryResult& out) noexcept;
  bool hasPendingWork() const noexcept;
  void clear() noexcept;
  // Split stop for the application shutdown coordinator. beginStop() only
  // wakes the query; joinUntil() keeps the worker context alive on timeout.
  void beginStop() noexcept;
  bool joinUntil(std::chrono::steady_clock::time_point deadline) noexcept;
  std::string diagnosticSnapshot() const;
  void stop() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace qingying::window_detail
