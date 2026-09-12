#pragma once

#include <cstdint>

#include <Windows.h>

#include "qingying/action/image.hpp"
#include "qingying/window/window_detector.hpp"

namespace qingying {

struct SmartRegionCandidate;

}  // namespace qingying

namespace qingying::window_detail {

enum class VisualRegionEdge : std::uint8_t {
  None = 0,
  Left = 1 << 0,
  Right = 1 << 1,
  Top = 1 << 2,
  Bottom = 1 << 3,
};

// 默认策略允许候选与宿主客户区的一条边相连，适用于传统的侧边栏和
// 窗口分区。Electron 工作台使用严格策略，避免将工作台边缘和代码纹理
// 拼接成并不存在的大区域。
enum class VisualRegionSearchPolicy : std::uint8_t {
  Standard,
  RequireCompleteBoundaries,
};

// 视觉候选的边界证据。edge_mask 标识真正从图像识别出的边；
// 候选至少需要一对相对边及第三条独立边，避免把文字碎片误识别为区域。
struct VisualRegionDiagnostic {
  WindowRect candidate;
  std::uint8_t edge_mask{0};
  std::uint8_t edge_coverage[4]{};
  std::uint8_t confidence{0};
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
                      VisualRegionSearchPolicy policy) noexcept;
bool findVisualRegion(const Image& background,
                      const WindowRect& image_screen_rect,
                      const WindowRect& owner_client_rect,
                      POINT screen_point, WindowRect& out,
                      VisualRegionDiagnostic& diagnostics,
                      VisualRegionSearchPolicy policy =
                          VisualRegionSearchPolicy::Standard) noexcept;
bool findVisualRegionCandidate(
    const Image& background, const WindowRect& image_screen_rect,
    const WindowRect& owner_client_rect, POINT screen_point,
    std::uintptr_t owner_window, SmartRegionCandidate& out,
    VisualRegionDiagnostic* diagnostics,
    VisualRegionSearchPolicy policy =
        VisualRegionSearchPolicy::Standard) noexcept;

}  // namespace qingying::window_detail
