#pragma once

#include <cstdint>

#include <Windows.h>

#include "qingying/action/image.hpp"
#include "qingying/window/window_detector.hpp"

namespace qingying::window_detail {

enum class VisualRegionEdge : std::uint8_t {
  None = 0,
  Left = 1 << 0,
  Right = 1 << 1,
  Top = 1 << 2,
  Bottom = 1 << 3,
};

// 视觉候选的边界证据。edge_mask 标识真正从图像识别出的边；
// 候选至少需要一对相对边及第三条独立边，避免把文字碎片误识别为区域。
struct VisualRegionDiagnostic {
  WindowRect candidate;
  std::uint8_t edge_mask{0};
  bool accepted{false};
};

// 在冻结背景图的鼠标邻域寻找具有连续边界的局部矩形。失败时不修改 out。
bool findVisualRegion(const Image& background,
                      const WindowRect& image_screen_rect,
                      const WindowRect& owner_client_rect,
                      POINT screen_point, WindowRect& out) noexcept;
bool findVisualRegion(const Image& background,
                      const WindowRect& image_screen_rect,
                      const WindowRect& owner_client_rect,
                      POINT screen_point, WindowRect& out,
                      VisualRegionDiagnostic& diagnostics) noexcept;

}  // namespace qingying::window_detail
