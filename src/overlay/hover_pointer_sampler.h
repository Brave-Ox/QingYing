#pragma once

#include <Windows.h>

#include <cstdint>

namespace qingying {

// Overlay 的 UI 线程使用此对象去重鼠标位置采样，避免任务栏等 Shell
// 表面未向 Overlay 投递 WM_MOUSEMOVE 时遗漏悬停更新。
class HoverPointerSampler final
{
 public:
  bool shouldProcess(POINT point) noexcept
  {
    const std::uint64_t now_ms = GetTickCount64();
    if (!m_has_last_point || point.x != m_last_point.x ||
        point.y != m_last_point.y)
    {
      m_last_point = point;
      m_has_last_point = true;
      m_stationary_recheck_pending = true;
      m_stationary_recheck_due_ms = now_ms + StationaryRecheckDelayMs;
      return true;
    }

    if (m_stationary_recheck_pending &&
        now_ms >= m_stationary_recheck_due_ms)
    {
      m_stationary_recheck_pending = false;
      return true;
    }

    return false;
  }

  void reset() noexcept
  {
    m_last_point = {};
    m_has_last_point = false;
    m_stationary_recheck_due_ms = 0;
    m_stationary_recheck_pending = false;
 }

 private:
  static constexpr std::uint64_t StationaryRecheckDelayMs = 64;

  POINT m_last_point{};
  std::uint64_t m_stationary_recheck_due_ms{0};
  bool m_has_last_point{false};
  bool m_stationary_recheck_pending{false};
};

}  // namespace qingying
