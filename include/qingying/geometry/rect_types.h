#pragma once

namespace qingying {

// A rectangle in physical desktop pixels. The origin may be negative when a
// monitor is positioned to the left or above the primary display.
struct ScreenPhysicalRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  constexpr int right() const noexcept { return x + width; }
  constexpr int bottom() const noexcept { return y + height; }
  constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
  constexpr bool valid() const noexcept { return !empty(); }
};

// A rectangle in an overlay window's client coordinates. Its origin is the
// overlay client area's (0, 0), so it must not be passed to screen-capture or
// window-discovery APIs without an explicit conversion.
struct OverlayClientRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  constexpr int right() const noexcept { return x + width; }
  constexpr int bottom() const noexcept { return y + height; }
  constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
  constexpr bool valid() const noexcept { return !empty(); }
};

// A rectangle relative to an Image's top-left pixel. ImagePixelRect is kept
// distinct from both desktop and overlay coordinates to make accidental
// cross-space arithmetic visible at the call site.
struct ImagePixelRect {
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  constexpr int right() const noexcept { return x + width; }
  constexpr int bottom() const noexcept { return y + height; }
  constexpr bool empty() const noexcept { return width <= 0 || height <= 0; }
  constexpr bool valid() const noexcept { return !empty(); }
};

constexpr bool operator==(const ScreenPhysicalRect& left,
                          const ScreenPhysicalRect& right) noexcept {
  return left.x == right.x && left.y == right.y &&
         left.width == right.width && left.height == right.height;
}

constexpr bool operator!=(const ScreenPhysicalRect& left,
                          const ScreenPhysicalRect& right) noexcept {
  return !(left == right);
}

constexpr bool operator==(const OverlayClientRect& left,
                          const OverlayClientRect& right) noexcept {
  return left.x == right.x && left.y == right.y &&
         left.width == right.width && left.height == right.height;
}

constexpr bool operator!=(const OverlayClientRect& left,
                          const OverlayClientRect& right) noexcept {
  return !(left == right);
}

constexpr bool operator==(const ImagePixelRect& left,
                          const ImagePixelRect& right) noexcept {
  return left.x == right.x && left.y == right.y &&
         left.width == right.width && left.height == right.height;
}

constexpr bool operator!=(const ImagePixelRect& left,
                          const ImagePixelRect& right) noexcept {
  return !(left == right);
}

}  // namespace qingying
