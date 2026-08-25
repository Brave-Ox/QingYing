#include "qingying/overlay/selection_handles.hpp"

#include <cstdlib>

namespace qingying {
namespace handles {

namespace {

bool within(int value, int lo, int hi) {
  return value >= lo && value <= hi;
}

void setPixel(std::vector<std::uint32_t>& pixels, int width, int height, int x,
              int y, std::uint32_t color) {
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return;
  }
  pixels.at(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
            static_cast<std::size_t>(x)) = color;
}

}  // namespace

SelectionHandle hitTest(int x, int y, int sx, int sy, int sw, int sh,
                        int radius) {
  if (sw <= 0 || sh <= 0) {
    return SelectionHandle::None;
  }

  const int left = sx;
  const int top = sy;
  const int right = sx + sw - 1;
  const int bottom = sy + sh - 1;

  const auto nearCorner = [&](int cx, int cy) {
    return std::abs(x - cx) <= radius && std::abs(y - cy) <= radius;
  };

  // 角优先，避免与边命中冲突。
  if (nearCorner(left, top)) {
    return SelectionHandle::TopLeft;
  }
  if (nearCorner(right, top)) {
    return SelectionHandle::TopRight;
  }
  if (nearCorner(left, bottom)) {
    return SelectionHandle::BottomLeft;
  }
  if (nearCorner(right, bottom)) {
    return SelectionHandle::BottomRight;
  }

  const bool in_x = within(x, left, right);
  const bool in_y = within(y, top, bottom);
  if (std::abs(x - left) <= radius && in_y) {
    return SelectionHandle::Left;
  }
  if (std::abs(x - right) <= radius && in_y) {
    return SelectionHandle::Right;
  }
  if (std::abs(y - top) <= radius && in_x) {
    return SelectionHandle::Top;
  }
  if (std::abs(y - bottom) <= radius && in_x) {
    return SelectionHandle::Bottom;
  }

  if (in_x && in_y) {
    return SelectionHandle::Move;
  }
  return SelectionHandle::None;
}

void drawHandles(std::vector<std::uint32_t>& pixels, int width, int height,
                 int sx, int sy, int sw, int sh, std::uint32_t color,
                 int radius) {
  if (sw <= 0 || sh <= 0 || width <= 0 || height <= 0) {
    return;
  }

  const int cx_mid = sx + sw / 2;
  const int cy_mid = sy + sh / 2;
  const int right = sx + sw - 1;
  const int bottom = sy + sh - 1;

  // 八个手柄中心：四角 + 四边中点。
  const int centers[8][2] = {
      {sx, sy},        {cx_mid, sy},   {right, sy},
      {right, cy_mid}, {right, bottom}, {cx_mid, bottom},
      {sx, bottom},    {sx, cy_mid},
  };

  for (const auto& c : centers) {
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        setPixel(pixels, width, height, c[0] + dx, c[1] + dy, color);
      }
    }
  }
}

}  // namespace handles
}  // namespace qingying
