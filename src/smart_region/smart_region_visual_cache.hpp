#pragma once

#include "qingying/window/smart_region_types.hpp"

namespace qingying {

// 缓存冻结截图同一小网格内的视觉检测结果，避免鼠标微动时重复扫描。
class SmartRegionVisualResultCache
{
 public:
  bool lookup(std::uintptr_t root_window, std::uintptr_t background_identity,
              const WindowRect& owner_rect, int screen_x, int screen_y,
              SmartRegionCandidate& out, bool& found,
              bool allow_browser_wide_fallback = false) const noexcept;
  void store(std::uintptr_t root_window, std::uintptr_t background_identity,
             const WindowRect& owner_rect, int screen_x, int screen_y,
             const SmartRegionCandidate* candidate,
             std::uint8_t minimum_visual_confidence = 0,
             bool allow_browser_wide_fallback = false) noexcept;
  void clear() noexcept;

 private:
  static constexpr std::size_t MaximumPositiveCandidates = 4;

  std::uintptr_t m_root_window{0};
  std::uintptr_t m_background_identity{0};
  WindowRect m_owner_rect;
  SmartRegionCandidate m_positive_candidates[MaximumPositiveCandidates];
  SmartRegionCandidate m_browser_wide_fallback;
  std::size_t m_positive_candidate_count{0};
  std::size_t m_next_positive_candidate_index{0};
  int m_browser_wide_fallback_cell_x{0};
  int m_browser_wide_fallback_cell_y{0};
  int m_negative_cell_x{0};
  int m_negative_cell_y{0};
  bool m_valid{false};
  bool m_has_browser_wide_fallback{false};
  bool m_has_negative_cell{false};
};

}  // namespace qingying
