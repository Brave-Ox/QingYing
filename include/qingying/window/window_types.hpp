#pragma once

#include "qingying/geometry/rect_types.h"
#include "qingying/window/window_query_types.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace qingying {

struct WindowCandidate {
  std::wstring title;
  std::uint32_t process_id{0};
  ScreenPhysicalRect bounds{};
  // An opaque discovery token, never a platform window handle.
  std::string window_token;
};

struct WindowCandidates {
  std::vector<WindowCandidate> candidates;
  bool truncated{false};
};

}  // namespace qingying
