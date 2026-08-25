#include "qingying/overlay/mask_renderer.hpp"

#include <algorithm>

namespace qingying {
namespace mask {

namespace {

// 遮罩层：约 35% 不透明黑（premultiplied alpha：alpha 0x59，RGB 0x00）。
constexpr std::uint32_t kMaskPixel = 0x59000000u;
// 选区内部：视觉上透明（alpha=1）以露出桌面，但 alpha 非 0 使分层窗口
// 在选区内仍可命中鼠标，从而支持「按住选区内部拖动整体移动」。
constexpr std::uint32_t kClearPixel = 0x01000000u;
// 选区边框：不透明亮橙，4px（premultiplied：alpha 0xFF，RGB 0xFF8000）。
constexpr std::uint32_t kBorderPixel = 0xFFFF8000u;
constexpr int kBorderThickness = 4;

std::uint32_t& pixelAt(std::vector<std::uint32_t>& px, int width, int x,
                       int y) {
  return px.at(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
               static_cast<std::size_t>(x));
}

// 判断点是否落在选区内缘边框（4px）上。边框画在选区内部边缘。
bool isOnBorder(int x, int y, int sel_left, int sel_top, int sel_right,
                int sel_bottom) {
  if (x < sel_left || x > sel_right || y < sel_top || y > sel_bottom) {
    return false;
  }
  const int dx_from_left = x - sel_left;
  const int dx_from_right = sel_right - x;
  const int dy_from_top = y - sel_top;
  const int dy_from_bottom = sel_bottom - y;
  return dx_from_left < kBorderThickness || dx_from_right < kBorderThickness ||
         dy_from_top < kBorderThickness || dy_from_bottom < kBorderThickness;
}

}  // namespace

void renderFullscreenMask(int width, int height,
                          const SelectionResult& selection,
                          std::vector<std::uint32_t>& out_pixels) {
  out_pixels.assign(static_cast<std::size_t>(width) *
                        static_cast<std::size_t>(height),
                    kMaskPixel);

  if (selection.cancelled) {
    return;  // 无有效选区：全屏遮罩，不挖空。
  }

  // 选区与屏幕求交，越界部分钳制到屏幕内。
  const int sel_left = std::max(0, selection.x);
  const int sel_top = std::max(0, selection.y);
  const int sel_right = std::min(width - 1, selection.x + selection.width - 1);
  const int sel_bottom =
      std::min(height - 1, selection.y + selection.height - 1);
  if (sel_left > sel_right || sel_top > sel_bottom) {
    return;  // 选区完全在屏幕外：保留全屏遮罩。
  }

  for (int y = sel_top; y <= sel_bottom; ++y) {
    for (int x = sel_left; x <= sel_right; ++x) {
      if (isOnBorder(x, y, sel_left, sel_top, sel_right, sel_bottom)) {
        pixelAt(out_pixels, width, x, y) = kBorderPixel;
      } else {
        pixelAt(out_pixels, width, x, y) = kClearPixel;
      }
    }
  }
}

}  // namespace mask
}  // namespace qingying
