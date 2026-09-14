#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
#include <memory>
#include <string>

#include <Windows.h>

#include "qingying/window/smart_region_detector.hpp"

namespace qingying::window_detail {

struct UiaRegionQueryRequest
{
  std::uint64_t request_id{0};
  HWND root_window{nullptr};
  POINT screen_point{};
  WindowRect owner_rect;
  std::uint64_t requested_at_ms{0};
  bool diagnostics_enabled{false};
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
};

using UiaRegionQueryFunction = void (*)(
    const UiaRegionQueryRequest& request, UiaRegionQueryResult& result,
    void* context) noexcept;

// 允许同一窗口内仍覆盖当前鼠标的近期结果合并，避免 UIA 查询慢于鼠标
// 更新节流时因请求号变化而被全部丢弃。跨窗口、超时或跨控件结果必须拒绝。
bool isUiaQueryResultApplicable(
    const UiaRegionQueryResult& result,
    const UiaRegionQueryRequest& current_request,
    std::uint64_t now_ms) noexcept;

// 仅保留固定预算内的 UIA 和 MSAA 候选，避免异步结果挤掉快速路径的回退链。
std::size_t retainAccessibilityCandidates(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    SmartRegionCandidate* out_candidates, std::size_t capacity) noexcept;

bool selectUiaQueryCandidate(const UiaRegionQueryResult& result,
                             const SmartRegionCandidate& fast_candidate,
                             POINT screen_point,
                             const WindowRect& owner_rect,
                             SmartRegionCandidate& out) noexcept;

// 在专用后台线程中串行执行跨进程 UIA 查询。请求采用 latest-wins 合并，
// 查询函数和 context 仅供内部测试替换；context 是不拥有的观察指针，必须
// 比工作器存活更久。
class UiaRegionQueryWorker
{
 public:
  explicit UiaRegionQueryWorker(
      UiaRegionQueryFunction query_function = nullptr,
      void* query_context = nullptr);
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
