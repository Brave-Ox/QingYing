#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

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
};

struct UiaRegionQueryResult
{
  std::uint64_t request_id{0};
  HWND root_window{nullptr};
  POINT screen_point{};
  WindowRect owner_rect;
  SmartRegionCandidate candidates[SmartRegionMaxCandidates];
  std::size_t candidate_count{0};
  std::uint64_t elapsed_ms{0};
  bool succeeded{false};
  bool msaa_attempted{false};
  bool cache_hit{false};
  bool suppressed_by_cooldown{false};
};

using UiaRegionQueryFunction = void (*)(
    const UiaRegionQueryRequest& request, UiaRegionQueryResult& result,
    void* context) noexcept;

bool selectUiaQueryCandidate(const UiaRegionQueryResult& result,
                             const SmartRegionCandidate& fast_candidate,
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
  void stop() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}  // namespace qingying::window_detail
