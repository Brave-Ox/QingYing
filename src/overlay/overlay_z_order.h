#pragma once

#include <Windows.h>

namespace qingying::overlay_detail {

inline bool isWindowAbove(HWND candidate, HWND reference) noexcept
{
  if (candidate == nullptr || reference == nullptr || candidate == reference ||
      IsWindow(candidate) == FALSE || IsWindow(reference) == FALSE)
  {
    return false;
  }

  for (HWND window = GetTopWindow(nullptr); window != nullptr;
       window = GetWindow(window, GW_HWNDNEXT))
  {
    if (window == candidate)
    {
      return true;
    }
    if (window == reference)
    {
      return false;
    }
  }
  return false;
}

inline bool ensureWindowAbove(HWND overlay, HWND obstruction) noexcept
{
  if (overlay == nullptr || obstruction == nullptr || overlay == obstruction ||
      IsWindow(overlay) == FALSE || IsWindow(obstruction) == FALSE)
  {
    return false;
  }
  if (!isWindowAbove(obstruction, overlay))
  {
    return true;
  }

  return SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
}

}  // namespace qingying::overlay_detail
