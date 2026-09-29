#pragma once

#include <Windows.h>

#include <cstddef>

#include "qingying/window/window_detector.hpp"

#include "../window/window_query_helpers.h"

namespace qingying::overlay_detail {

struct TaskbarSnapshot
{
  HWND window{nullptr};
  WindowRect rect;
};

// 截图会话创建 Overlay 前保存 Shell 任务栏的边界；命中时不再依赖
// Overlay 显示后 Shell 窗口的可见性或 Z 序。
class TaskbarSnapshotSet final
{
 public:
  static constexpr std::size_t MaxTaskbars = 8;

  bool add(HWND window, const WindowRect& rect) noexcept;
  bool findAt(int screen_x, int screen_y, HWND& out_window,
              WindowRect& out_rect) const noexcept;
  void captureVisibleTaskbars() noexcept;
  void clear() noexcept;
  std::size_t count() const noexcept;

 private:
  TaskbarSnapshot m_taskbars[MaxTaskbars]{};
  std::size_t m_count{0};
};

inline bool TaskbarSnapshotSet::add(HWND window,
                                    const WindowRect& rect) noexcept
{
  if (window == nullptr || rect.empty() || m_count >= MaxTaskbars)
  {
    return false;
  }

  for (std::size_t index = 0; index < m_count; ++index)
  {
    if (m_taskbars[index].window == window)
    {
      return false;
    }
  }

  m_taskbars[m_count++] = {window, rect};
  return true;
}

inline bool TaskbarSnapshotSet::findAt(int screen_x, int screen_y,
                                       HWND& out_window,
                                       WindowRect& out_rect) const noexcept
{
  out_window = nullptr;
  out_rect = {};
  for (std::size_t index = 0; index < m_count; ++index)
  {
    const TaskbarSnapshot& snapshot = m_taskbars[index];
    if (snapshot.window == nullptr || IsWindow(snapshot.window) == FALSE ||
        screen_x < snapshot.rect.left || screen_x >= snapshot.rect.right ||
        screen_y < snapshot.rect.top || screen_y >= snapshot.rect.bottom)
    {
      continue;
    }

    out_window = snapshot.window;
    out_rect = snapshot.rect;
    return true;
  }
  return false;
}

namespace taskbar_snapshot_detail {

inline BOOL CALLBACK captureTaskbar(HWND window, LPARAM context) noexcept
{
  TaskbarSnapshotSet* const snapshots =
      reinterpret_cast<TaskbarSnapshotSet*>(context);
  if (snapshots == nullptr || !window_detail::isTaskbarWindow(window) ||
      IsWindowVisible(window) == FALSE || IsIconic(window) != FALSE ||
      window_detail::cloaked(window))
  {
    return TRUE;
  }

  RECT rect{};
  if (GetWindowRect(window, &rect) != FALSE)
  {
    static_cast<void>(snapshots->add(
        window, {rect.left, rect.top, rect.right, rect.bottom}));
  }
  return TRUE;
}

}  // namespace taskbar_snapshot_detail

inline void TaskbarSnapshotSet::captureVisibleTaskbars() noexcept
{
  clear();
  static_cast<void>(EnumWindows(
      taskbar_snapshot_detail::captureTaskbar,
      reinterpret_cast<LPARAM>(this)));
}

inline void TaskbarSnapshotSet::clear() noexcept
{
  for (std::size_t index = 0; index < m_count; ++index)
  {
    m_taskbars[index] = {};
  }
  m_count = 0;
}

inline std::size_t TaskbarSnapshotSet::count() const noexcept
{
  return m_count;
}

}  // namespace qingying::overlay_detail
