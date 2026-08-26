#pragma once

#include <cstdint>

namespace qingying {

// A supported application's scroll target and content bounds. The bounds use
// physical screen pixels and are only used to validate the user's selection;
// they never replace the requested capture rectangle.
struct LongShotProfileResult {
  std::uintptr_t scroll_target{0};
  int content_x{0};
  int content_y{0};
  int content_width{0};
  int content_height{0};

  bool valid() const;
  bool containsSelection(int x, int y, int width, int height) const;
};

// Resolves only the supplied top-level window. This function never queries the
// foreground window and never chooses a capture rectangle for the caller.
bool resolveNotepadProfile(std::uintptr_t owner_window,
                           LongShotProfileResult& out);

}  // namespace qingying
