#include "visual_region_locator.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace qingying::window_detail {
namespace {

constexpr int kMinimumRegionWidth = 48;
constexpr int kMinimumRegionHeight = 36;
constexpr int kMaximumSearchDistance = 640;
constexpr int kSampleStride = 4;
constexpr int kMinimumSamples = 8;
constexpr int kEdgeColorDifference = 36;
constexpr int kRequiredEdgeCoveragePercent = 60;
constexpr int kLocalProbeHalfExtent = 96;
constexpr std::int64_t kMaximumRegionPercent = 85;

struct SearchBounds {
  int left{0};
  int top{0};
  int right{0};
  int bottom{0};
};

bool contains(const WindowRect& rect, POINT point) noexcept
{
  return !rect.empty() && point.x >= rect.left && point.x < rect.right &&
         point.y >= rect.top && point.y < rect.bottom;
}

bool isValidImage(const Image& image, const WindowRect& image_screen_rect)
    noexcept
{
  if (image.empty() || image_screen_rect.empty() ||
      image.width != image_screen_rect.width() ||
      image.height != image_screen_rect.height()) {
    return false;
  }
  const std::size_t expected_pixels =
      static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height);
  return image.pixels.size() >= expected_pixels;
}

SearchBounds intersection(const WindowRect& left,
                          const WindowRect& right) noexcept
{
  return {(std::max)(left.left, right.left), (std::max)(left.top, right.top),
          (std::min)(left.right, right.right),
          (std::min)(left.bottom, right.bottom)};
}

bool valid(const SearchBounds& bounds) noexcept
{
  return bounds.right > bounds.left && bounds.bottom > bounds.top;
}

SearchBounds localProbeBounds(const SearchBounds& bounds,
                              POINT point) noexcept
{
  const int point_x = static_cast<int>(point.x);
  const int point_y = static_cast<int>(point.y);
  return {(std::max)(bounds.left, point_x - kLocalProbeHalfExtent),
          (std::max)(bounds.top, point_y - kLocalProbeHalfExtent),
          (std::min)(bounds.right, point_x + kLocalProbeHalfExtent),
          (std::min)(bounds.bottom, point_y + kLocalProbeHalfExtent)};
}

std::uint32_t pixelAt(const Image& image,
                      const WindowRect& image_screen_rect, int screen_x,
                      int screen_y) noexcept
{
  const int image_x = screen_x - image_screen_rect.left;
  const int image_y = screen_y - image_screen_rect.top;
  return image.pixels[static_cast<std::size_t>(image_y) *
                          static_cast<std::size_t>(image.width) +
                      static_cast<std::size_t>(image_x)];
}

int channelDifference(std::uint32_t left, std::uint32_t right,
                      int shift) noexcept
{
  const int left_channel = static_cast<int>((left >> shift) & 0xFFu);
  const int right_channel = static_cast<int>((right >> shift) & 0xFFu);
  return left_channel >= right_channel ? left_channel - right_channel
                                       : right_channel - left_channel;
}

bool isStrongEdge(std::uint32_t left, std::uint32_t right) noexcept
{
  const int difference = channelDifference(left, right, 0) +
                         channelDifference(left, right, 8) +
                         channelDifference(left, right, 16);
  return difference >= kEdgeColorDifference;
}

bool hasVerticalEdge(const Image& image, const WindowRect& image_screen_rect,
                     const SearchBounds& bounds, int screen_x,
                     int sample_top, int sample_bottom) noexcept
{
  if (screen_x <= bounds.left || screen_x >= bounds.right ||
      sample_bottom <= sample_top) {
    return false;
  }
  int samples = 0;
  int edge_samples = 0;
  for (int y = sample_top; y < sample_bottom; y += kSampleStride) {
    ++samples;
    if (isStrongEdge(pixelAt(image, image_screen_rect, screen_x - 1, y),
                     pixelAt(image, image_screen_rect, screen_x, y))) {
      ++edge_samples;
    }
  }
  return samples >= kMinimumSamples &&
         edge_samples * 100 >= samples * kRequiredEdgeCoveragePercent;
}

bool hasHorizontalEdge(const Image& image,
                       const WindowRect& image_screen_rect,
                       const SearchBounds& bounds, int screen_y,
                       int sample_left, int sample_right,
                       int required_coverage_percent) noexcept
{
  if (screen_y <= bounds.top || screen_y >= bounds.bottom ||
      sample_right <= sample_left) {
    return false;
  }
  int samples = 0;
  int edge_samples = 0;
  for (int x = sample_left; x < sample_right; x += kSampleStride) {
    ++samples;
    if (isStrongEdge(pixelAt(image, image_screen_rect, x, screen_y - 1),
                     pixelAt(image, image_screen_rect, x, screen_y))) {
      ++edge_samples;
    }
  }
  return samples >= kMinimumSamples &&
         edge_samples * 100 >= samples * required_coverage_percent;
}

int findVerticalBoundary(const Image& image,
                         const WindowRect& image_screen_rect,
                         const SearchBounds& bounds, POINT point,
                         int sample_top, int sample_bottom, bool search_left,
                         bool& found) noexcept
{
  found = false;
  const int point_x = static_cast<int>(point.x);
  const int start = search_left ? point_x - 1 : point_x + 1;
  const int limit = search_left
                        ? (std::max)(bounds.left + 1,
                                     point_x - kMaximumSearchDistance)
                        : (std::min)(bounds.right - 1,
                                     point_x + kMaximumSearchDistance);
  if (search_left) {
    for (int x = start; x >= limit; --x) {
      if (hasVerticalEdge(image, image_screen_rect, bounds, x, sample_top,
                          sample_bottom)) {
        found = true;
        return x;
      }
    }
    return bounds.left;
  }
  for (int x = start; x <= limit; ++x) {
    if (hasVerticalEdge(image, image_screen_rect, bounds, x, sample_top,
                        sample_bottom)) {
      found = true;
      return x;
    }
  }
  return bounds.right;
}

int findHorizontalBoundary(const Image& image,
                           const WindowRect& image_screen_rect,
                           const SearchBounds& bounds, POINT point,
                           int sample_left, int sample_right, bool search_top,
                           int required_coverage_percent, bool& found) noexcept
{
  found = false;
  const int point_y = static_cast<int>(point.y);
  const int start = search_top ? point_y - 1 : point_y + 1;
  const int limit = search_top
                        ? (std::max)(bounds.top + 1,
                                     point_y - kMaximumSearchDistance)
                        : (std::min)(bounds.bottom - 1,
                                     point_y + kMaximumSearchDistance);
  if (search_top) {
    for (int y = start; y >= limit; --y) {
      if (hasHorizontalEdge(image, image_screen_rect, bounds, y, sample_left,
                            sample_right, required_coverage_percent)) {
        found = true;
        return y;
      }
    }
    return bounds.top;
  }
  for (int y = start; y <= limit; ++y) {
    if (hasHorizontalEdge(image, image_screen_rect, bounds, y, sample_left,
                          sample_right, required_coverage_percent)) {
      found = true;
      return y;
    }
  }
  return bounds.bottom;
}

std::int64_t areaOf(const WindowRect& rect) noexcept
{
  return static_cast<std::int64_t>(rect.width()) * rect.height();
}

}  // namespace

bool findVisualRegion(const Image& background,
                      const WindowRect& image_screen_rect,
                      const WindowRect& owner_client_rect,
                      POINT screen_point, WindowRect& out) noexcept
{
  VisualRegionDiagnostic diagnostics;
  return findVisualRegion(background, image_screen_rect, owner_client_rect,
                          screen_point, out, diagnostics);
}

bool findVisualRegion(const Image& background,
                      const WindowRect& image_screen_rect,
                      const WindowRect& owner_client_rect,
                      POINT screen_point, WindowRect& out,
                      VisualRegionDiagnostic& diagnostics) noexcept
{
  diagnostics = VisualRegionDiagnostic{};
  if (!isValidImage(background, image_screen_rect) ||
      !contains(owner_client_rect, screen_point) ||
      !contains(image_screen_rect, screen_point)) {
    return false;
  }

  const SearchBounds bounds = intersection(owner_client_rect, image_screen_rect);
  if (!valid(bounds)) {
    return false;
  }

  const SearchBounds local_bounds = localProbeBounds(bounds, screen_point);
  if (!valid(local_bounds)) {
    return false;
  }

  bool has_left_edge = false;
  bool has_right_edge = false;
  bool has_top_edge = false;
  bool has_bottom_edge = false;

  // 先在鼠标邻域内找竖边。窄侧栏不会因为其宽度远小于窗口而被整窗采样稀释。
  int left = findVerticalBoundary(background, image_screen_rect, bounds,
                                  screen_point, local_bounds.top,
                                  local_bounds.bottom, true, has_left_edge);
  int right = findVerticalBoundary(background, image_screen_rect, bounds,
                                   screen_point, local_bounds.top,
                                   local_bounds.bottom, false, has_right_edge);
  int top = bounds.top;
  int bottom = bounds.bottom;

  if (has_left_edge && has_right_edge) {
    // 已获得局部竖边后，按候选自身宽度寻找上下边界，可识别高而窄的侧栏。
    top = findHorizontalBoundary(
        background, image_screen_rect, bounds, screen_point, left, right,
        true, kRequiredEdgeCoveragePercent, has_top_edge);
    bottom = findHorizontalBoundary(
        background, image_screen_rect, bounds, screen_point, left, right,
        false, kRequiredEdgeCoveragePercent, has_bottom_edge);
  }

  if (!has_top_edge || !has_bottom_edge) {
    // 未找到成对竖边时，先用鼠标邻域内的横边确定短列表行的高度。
    top = findHorizontalBoundary(
        background, image_screen_rect, bounds, screen_point,
        local_bounds.left, local_bounds.right, true,
        kRequiredEdgeCoveragePercent, has_top_edge);
    bottom = findHorizontalBoundary(
        background, image_screen_rect, bounds, screen_point,
        local_bounds.left, local_bounds.right, false,
        kRequiredEdgeCoveragePercent, has_bottom_edge);
  }

  if (has_top_edge && has_bottom_edge &&
      (!has_left_edge || !has_right_edge)) {
    // 已获得局部横边后，按候选自身高度寻找左右边界，可识别短而窄的列表行。
    left = findVerticalBoundary(background, image_screen_rect, bounds,
                                screen_point, top, bottom, true,
                                has_left_edge);
    right = findVerticalBoundary(background, image_screen_rect, bounds,
                                 screen_point, top, bottom, false,
                                 has_right_edge);
  }
  const WindowRect candidate{left, top, right, bottom};
  diagnostics.candidate = candidate;
  if (has_left_edge) {
    diagnostics.edge_mask |= static_cast<std::uint8_t>(VisualRegionEdge::Left);
  }
  if (has_right_edge) {
    diagnostics.edge_mask |= static_cast<std::uint8_t>(VisualRegionEdge::Right);
  }
  if (has_top_edge) {
    diagnostics.edge_mask |= static_cast<std::uint8_t>(VisualRegionEdge::Top);
  }
  if (has_bottom_edge) {
    diagnostics.edge_mask |= static_cast<std::uint8_t>(VisualRegionEdge::Bottom);
  }
  const int edge_count = static_cast<int>(has_left_edge) +
                         static_cast<int>(has_right_edge) +
                         static_cast<int>(has_top_edge) +
                         static_cast<int>(has_bottom_edge);
  const bool has_horizontal_pair = has_top_edge && has_bottom_edge;
  const bool has_vertical_pair = has_left_edge && has_right_edge;
  if (edge_count < 3 || (!has_horizontal_pair && !has_vertical_pair) ||
      candidate.width() < kMinimumRegionWidth ||
      candidate.height() < kMinimumRegionHeight ||
      areaOf(candidate) * 100 >=
          areaOf(owner_client_rect) * kMaximumRegionPercent) {
    return false;
  }

  out = candidate;
  diagnostics.accepted = true;
  return true;
}

}  // namespace qingying::window_detail
