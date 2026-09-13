#include "visual_region_locator.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "qingying/window/smart_region_detector.hpp"

namespace qingying::window_detail {
namespace {

constexpr int kMinimumRegionWidth = 48;
constexpr int kMinimumRegionHeight = 36;
constexpr int kMinimumCompactRegionWidth = 24;
constexpr int kMinimumCompactRegionHeight = 20;
constexpr int kMaximumCompactRegionWidth = 320;
constexpr int kMaximumCompactRegionHeight = 160;
constexpr int kMaximumSearchDistance = 640;
constexpr int kSampleStride = 4;
constexpr int kMinimumSamples = 8;
constexpr int kMinimumCompactSamples = 4;
constexpr int kEdgeColorDifference = 36;
constexpr int kWeakEdgeColorDifference = 18;
constexpr int kRequiredEdgeCoveragePercent = 60;
constexpr int kLocalProbeHalfExtent = 96;
constexpr int kCompactProbeHalfExtent = 12;
constexpr std::int64_t kMaximumRegionPercent = 85;
constexpr std::int64_t kWorkbenchIncompleteMaximumOwnerSpanPercent = 50;
constexpr int kWorkbenchGlobalSeparatorCoveragePercent = 45;
constexpr ULONGLONG kWorkbenchGlobalSeparatorBudgetMs = 16;
constexpr std::size_t kMaximumConfirmedBoundaries = 3;
constexpr std::size_t kMaximumBoundaryCandidates =
    kMaximumConfirmedBoundaries + 1;
constexpr int kExteriorSampleOffset = 2;
constexpr int kExteriorRangeForZeroConfidence = 96;

struct SearchBounds {
  int left{0};
  int top{0};
  int right{0};
  int bottom{0};
};

struct BoundaryCandidate
{
  int coordinate{0};
  int coverage{0};
  int exterior_consistency{0};
  bool confirmed{false};
};

struct BoundaryCandidates
{
  std::array<BoundaryCandidate, kMaximumBoundaryCandidates> values{};
  std::size_t count{0};
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

SearchBounds localSampleBounds(const SearchBounds& available_bounds,
                               POINT point) noexcept
{
  const int point_x = static_cast<int>(point.x);
  const int point_y = static_cast<int>(point.y);
  return {(std::max)(available_bounds.left,
                     point_x - kLocalProbeHalfExtent),
          (std::max)(available_bounds.top,
                     point_y - kLocalProbeHalfExtent),
          (std::min)(available_bounds.right,
                     point_x + kLocalProbeHalfExtent),
          (std::min)(available_bounds.bottom,
                     point_y + kLocalProbeHalfExtent)};
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

bool hasColorDifference(std::uint32_t left, std::uint32_t right,
                        int minimum_difference) noexcept
{
  const int difference = channelDifference(left, right, 0) +
                         channelDifference(left, right, 8) +
                         channelDifference(left, right, 16);
  return difference >= minimum_difference;
}

int verticalEdgeCoverage(const Image& image,
                         const WindowRect& image_screen_rect,
                         const SearchBounds& bounds, int screen_x,
                         int sample_top, int sample_bottom,
                         int minimum_difference = kEdgeColorDifference,
                         int minimum_samples = kMinimumSamples) noexcept
{
  if (screen_x <= bounds.left || screen_x >= bounds.right ||
      sample_bottom <= sample_top)
  {
    return 0;
  }

  int samples = 0;
  int edge_samples = 0;
  for (int y = sample_top; y < sample_bottom; y += kSampleStride)
  {
    ++samples;
    if (hasColorDifference(
            pixelAt(image, image_screen_rect, screen_x - 1, y),
            pixelAt(image, image_screen_rect, screen_x, y),
            minimum_difference))
    {
      ++edge_samples;
    }
  }
  return samples >= minimum_samples ? edge_samples * 100 / samples : 0;
}

int horizontalEdgeCoverage(const Image& image,
                           const WindowRect& image_screen_rect,
                           const SearchBounds& bounds, int screen_y,
                           int sample_left, int sample_right,
                           int minimum_difference = kEdgeColorDifference,
                           int minimum_samples = kMinimumSamples) noexcept
{
  if (screen_y <= bounds.top || screen_y >= bounds.bottom ||
      sample_right <= sample_left)
  {
    return 0;
  }

  int samples = 0;
  int edge_samples = 0;
  for (int x = sample_left; x < sample_right; x += kSampleStride)
  {
    ++samples;
    if (hasColorDifference(
            pixelAt(image, image_screen_rect, x, screen_y - 1),
            pixelAt(image, image_screen_rect, x, screen_y),
            minimum_difference))
    {
      ++edge_samples;
    }
  }
  return samples >= minimum_samples ? edge_samples * 100 / samples : 0;
}

int colorRangeConsistency(const Image& image,
                          const WindowRect& image_screen_rect,
                          bool vertical, int fixed_coordinate, int begin,
                          int end,
                          int minimum_samples = kMinimumSamples) noexcept
{
  int minimum[3] = {255, 255, 255};
  int maximum[3] = {0, 0, 0};
  int samples = 0;
  for (int coordinate = begin; coordinate < end;
       coordinate += kSampleStride)
  {
    const int screen_x = vertical ? fixed_coordinate : coordinate;
    const int screen_y = vertical ? coordinate : fixed_coordinate;
    const std::uint32_t pixel =
        pixelAt(image, image_screen_rect, screen_x, screen_y);
    for (int channel = 0; channel < 3; ++channel)
    {
      const int value =
          static_cast<int>((pixel >> (channel * 8)) & 0xFFu);
      minimum[channel] = (std::min)(minimum[channel], value);
      maximum[channel] = (std::max)(maximum[channel], value);
    }
    ++samples;
  }
  if (samples < minimum_samples)
  {
    return 0;
  }

  const int range = maximum[0] - minimum[0] + maximum[1] - minimum[1] +
                    maximum[2] - minimum[2];
  return (std::max)(0, 100 - range * 100 / kExteriorRangeForZeroConfidence);
}

int boundaryQuality(const BoundaryCandidate& candidate) noexcept
{
  return candidate.coverage * 2 + candidate.exterior_consistency;
}

void retainBoundary(BoundaryCandidates& candidates,
                    const BoundaryCandidate& candidate) noexcept
{
  if (candidates.count < kMaximumConfirmedBoundaries)
  {
    candidates.values.at(candidates.count++) = candidate;
    return;
  }

  std::size_t weakest = 0;
  for (std::size_t index = 1; index < candidates.count; ++index)
  {
    if (boundaryQuality(candidates.values.at(index)) <
        boundaryQuality(candidates.values.at(weakest)))
    {
      weakest = index;
    }
  }
  if (boundaryQuality(candidate) >
      boundaryQuality(candidates.values.at(weakest)))
  {
    candidates.values.at(weakest) = candidate;
  }
}

BoundaryCandidates collectVerticalBoundaries(
    const Image& image, const WindowRect& image_screen_rect,
    const SearchBounds& bounds, POINT point, int sample_top,
    int sample_bottom, bool search_left,
    int minimum_difference = kEdgeColorDifference,
    int minimum_samples = kMinimumSamples) noexcept
{
  BoundaryCandidates candidates;
  const int point_x = static_cast<int>(point.x);
  const int start = search_left ? point_x - 1 : point_x + 1;
  const int limit = search_left
                        ? (std::max)(bounds.left + 1,
                                     point_x - kMaximumSearchDistance)
                        : (std::min)(bounds.right - 1,
                                     point_x + kMaximumSearchDistance);
  const int step = search_left ? -1 : 1;
  bool inside_edge = false;
  for (int x = start; search_left ? x >= limit : x <= limit; x += step)
  {
    const int coverage = verticalEdgeCoverage(
        image, image_screen_rect, bounds, x, sample_top, sample_bottom,
        minimum_difference, minimum_samples);
    const bool strong = coverage >= kRequiredEdgeCoveragePercent;
    if (strong && !inside_edge)
    {
      const int exterior_x = search_left ? x - kExteriorSampleOffset
                                         : x + kExteriorSampleOffset;
      const int consistency =
          exterior_x > bounds.left && exterior_x < bounds.right
              ? colorRangeConsistency(image, image_screen_rect, true,
                                      exterior_x, sample_top, sample_bottom,
                                      minimum_samples)
              : 0;
      retainBoundary(candidates, {x, coverage, consistency, true});
    }
    inside_edge = strong;
  }

  candidates.values.at(candidates.count++) =
      {search_left ? bounds.left : bounds.right, 0, 0, false};
  return candidates;
}

BoundaryCandidates collectHorizontalBoundaries(
    const Image& image, const WindowRect& image_screen_rect,
    const SearchBounds& bounds, POINT point, int sample_left,
    int sample_right, bool search_top,
    int minimum_difference = kEdgeColorDifference,
    int minimum_samples = kMinimumSamples) noexcept
{
  BoundaryCandidates candidates;
  const int point_y = static_cast<int>(point.y);
  const int start = search_top ? point_y - 1 : point_y + 1;
  const int limit = search_top
                        ? (std::max)(bounds.top + 1,
                                     point_y - kMaximumSearchDistance)
                        : (std::min)(bounds.bottom - 1,
                                     point_y + kMaximumSearchDistance);
  const int step = search_top ? -1 : 1;
  bool inside_edge = false;
  for (int y = start; search_top ? y >= limit : y <= limit; y += step)
  {
    const int coverage = horizontalEdgeCoverage(
        image, image_screen_rect, bounds, y, sample_left, sample_right,
        minimum_difference, minimum_samples);
    const bool strong = coverage >= kRequiredEdgeCoveragePercent;
    if (strong && !inside_edge)
    {
      const int exterior_y = search_top ? y - kExteriorSampleOffset
                                        : y + kExteriorSampleOffset;
      const int consistency =
          exterior_y > bounds.top && exterior_y < bounds.bottom
              ? colorRangeConsistency(image, image_screen_rect, false,
                                      exterior_y, sample_left, sample_right,
                                      minimum_samples)
              : 0;
      retainBoundary(candidates, {y, coverage, consistency, true});
    }
    inside_edge = strong;
  }

  candidates.values.at(candidates.count++) =
      {search_top ? bounds.top : bounds.bottom, 0, 0, false};
  return candidates;
}

std::int64_t areaOf(const WindowRect& rect) noexcept
{
  return static_cast<std::int64_t>(rect.width()) * rect.height();
}

bool missingBoundaryIsAttachedToOwner(
    const BoundaryCandidate& left, const BoundaryCandidate& right,
    const BoundaryCandidate& top, const BoundaryCandidate& bottom,
    const WindowRect& owner_client_rect) noexcept
{
  return (left.confirmed || left.coordinate == owner_client_rect.left) &&
         (right.confirmed || right.coordinate == owner_client_rect.right) &&
         (top.confirmed || top.coordinate == owner_client_rect.top) &&
         (bottom.confirmed || bottom.coordinate == owner_client_rect.bottom);
}

int rectangleEvidenceScore(const BoundaryCandidate& left,
                           const BoundaryCandidate& right,
                           const BoundaryCandidate& top,
                           const BoundaryCandidate& bottom) noexcept
{
  return boundaryQuality(left) + boundaryQuality(right) +
         boundaryQuality(top) + boundaryQuality(bottom);
}

BoundaryCandidate findNearestGlobalVerticalSeparator(
    const Image& image, const WindowRect& image_screen_rect,
    const SearchBounds& bounds, POINT point, bool search_left,
    ULONGLONG start_time_ms) noexcept
{
  const int start = search_left ? static_cast<int>(point.x) - 1
                                : static_cast<int>(point.x) + 1;
  const int limit = search_left ? bounds.left + 1 : bounds.right - 1;
  const int step = search_left ? -1 : 1;
  for (int x = start; search_left ? x >= limit : x <= limit; x += step)
  {
    if (GetTickCount64() - start_time_ms >
        kWorkbenchGlobalSeparatorBudgetMs)
    {
      break;
    }
    const int coverage = verticalEdgeCoverage(
        image, image_screen_rect, bounds, x, bounds.top, bounds.bottom);
    if (coverage >= kWorkbenchGlobalSeparatorCoveragePercent)
    {
      return {x, coverage, 0, true};
    }
  }
  return {search_left ? bounds.left : bounds.right, 0, 0, false};
}

BoundaryCandidate findNearestGlobalHorizontalSeparator(
    const Image& image, const WindowRect& image_screen_rect,
    const SearchBounds& bounds, POINT point, bool search_top,
    ULONGLONG start_time_ms) noexcept
{
  const int start = search_top ? static_cast<int>(point.y) - 1
                               : static_cast<int>(point.y) + 1;
  const int limit = search_top ? bounds.top + 1 : bounds.bottom - 1;
  const int step = search_top ? -1 : 1;
  for (int y = start; search_top ? y >= limit : y <= limit; y += step)
  {
    if (GetTickCount64() - start_time_ms >
        kWorkbenchGlobalSeparatorBudgetMs)
    {
      break;
    }
    const int coverage = horizontalEdgeCoverage(
        image, image_screen_rect, bounds, y, bounds.left, bounds.right);
    if (coverage >= kWorkbenchGlobalSeparatorCoveragePercent)
    {
      return {y, coverage, 0, true};
    }
  }
  return {search_top ? bounds.top : bounds.bottom, 0, 0, false};
}

bool findWorkbenchPanelByGlobalSeparators(
    const Image& image, const WindowRect& image_screen_rect,
    const SearchBounds& bounds, POINT point,
    const WindowRect& owner_client_rect, WindowRect& out,
    VisualRegionDiagnostic& diagnostics,
    bool allow_owner_attached_boundaries) noexcept
{
  const ULONGLONG start_time_ms = GetTickCount64();
  const BoundaryCandidate left = findNearestGlobalVerticalSeparator(
      image, image_screen_rect, bounds, point, true, start_time_ms);
  const BoundaryCandidate right = findNearestGlobalVerticalSeparator(
      image, image_screen_rect, bounds, point, false, start_time_ms);
  const SearchBounds panel_column_bounds{
      left.confirmed ? left.coordinate : bounds.left, bounds.top,
      right.confirmed ? right.coordinate : bounds.right, bounds.bottom};
  if (!valid(panel_column_bounds) ||
      GetTickCount64() - start_time_ms > kWorkbenchGlobalSeparatorBudgetMs)
  {
    return false;
  }
  const BoundaryCandidate top = findNearestGlobalHorizontalSeparator(
      image, image_screen_rect, panel_column_bounds, point, true,
      start_time_ms);
  const BoundaryCandidate bottom = findNearestGlobalHorizontalSeparator(
      image, image_screen_rect, panel_column_bounds, point, false,
      start_time_ms);
  const int confirmed_edge_count = static_cast<int>(left.confirmed) +
                                   static_cast<int>(right.confirmed) +
                                   static_cast<int>(top.confirmed) +
                                   static_cast<int>(bottom.confirmed);
  const int minimum_confirmed_edge_count =
      allow_owner_attached_boundaries ? 3 : 4;
  if (confirmed_edge_count < minimum_confirmed_edge_count ||
      (allow_owner_attached_boundaries &&
       !missingBoundaryIsAttachedToOwner(left, right, top, bottom,
                                         owner_client_rect)))
  {
    return false;
  }

  const WindowRect candidate{left.coordinate, top.coordinate,
                             right.coordinate, bottom.coordinate};
  if (candidate.width() < kMinimumRegionWidth ||
      candidate.height() < kMinimumRegionHeight ||
      !contains(candidate, point) ||
      areaOf(candidate) * 100 >=
          areaOf(owner_client_rect) * kMaximumRegionPercent)
  {
    return false;
  }

  diagnostics.candidate = candidate;
  constexpr VisualRegionEdge EdgeKinds[4] = {
      VisualRegionEdge::Left, VisualRegionEdge::Right,
      VisualRegionEdge::Top, VisualRegionEdge::Bottom};
  const BoundaryCandidate edges[4] = {left, right, top, bottom};
  for (std::size_t index = 0; index < std::size(edges); ++index)
  {
    if (edges[index].confirmed)
    {
      diagnostics.edge_mask |= static_cast<std::uint8_t>(EdgeKinds[index]);
    }
  }
  diagnostics.edge_coverage[0] = static_cast<std::uint8_t>(left.coverage);
  diagnostics.edge_coverage[1] = static_cast<std::uint8_t>(right.coverage);
  diagnostics.edge_coverage[2] = static_cast<std::uint8_t>(top.coverage);
  diagnostics.edge_coverage[3] = static_cast<std::uint8_t>(bottom.coverage);
  diagnostics.confidence = static_cast<std::uint8_t>((std::min)(
      100, rectangleEvidenceScore(left, right, top, bottom) * 100 / 800));
  diagnostics.accepted = true;
  out = candidate;
  return true;
}

}  // namespace

bool findVisualRegion(const Image& background,
                      const WindowRect& image_screen_rect,
                      const WindowRect& owner_client_rect,
                      POINT screen_point, WindowRect& out) noexcept
{
  VisualRegionDiagnostic diagnostics;
  return findVisualRegion(background, image_screen_rect, owner_client_rect,
                          screen_point, out, diagnostics,
                          VisualRegionSearchPolicy::Standard);
}

bool findVisualRegion(const Image& background,
                      const WindowRect& image_screen_rect,
                      const WindowRect& owner_client_rect,
                      POINT screen_point, WindowRect& out,
                      VisualRegionSearchPolicy policy) noexcept
{
  VisualRegionDiagnostic diagnostics;
  return findVisualRegion(background, image_screen_rect, owner_client_rect,
                          screen_point, out, diagnostics, policy);
}

bool findVisualRegion(const Image& background,
                      const WindowRect& image_screen_rect,
                      const WindowRect& owner_client_rect,
                      POINT screen_point, WindowRect& out,
                      VisualRegionDiagnostic& diagnostics,
                      VisualRegionSearchPolicy policy) noexcept
{
  diagnostics = VisualRegionDiagnostic{};
  if (!isValidImage(background, image_screen_rect) ||
      !contains(owner_client_rect, screen_point) ||
      !contains(image_screen_rect, screen_point)) {
    return false;
  }

  // available_bounds 是既属于 Owner、又存在冻结截图像素的可扫描区域。
  // 它的边缘不一定是 Owner 边缘，不能直接作为缺失控件边界使用。
  const SearchBounds available_bounds =
      intersection(owner_client_rect, image_screen_rect);
  if (!valid(available_bounds)) {
    return false;
  }

  // Electron 工作台面板不能复用通用控件扫描：编辑器与终端的分隔线
  // 通常只覆盖本列/本行，且通用扫描会把相邻面板合成一个大候选。该路径
  // 只依据穿过鼠标所在列的分隔线，并受独立 16ms 总预算保护。
  if (policy == VisualRegionSearchPolicy::ElectronWorkbench)
  {
    return findWorkbenchPanelByGlobalSeparators(
        background, image_screen_rect, available_bounds, screen_point,
        owner_client_rect, out, diagnostics, true);
  }

  // local_sample_bounds 只限制边界覆盖率的采样跨度；候选边界仍可在
  // available_bounds 中向外搜索，因此宽行和大卡片不会被局部采样裁断。
  const SearchBounds local_sample_bounds =
      localSampleBounds(available_bounds, screen_point);
  if (!valid(local_sample_bounds)) {
    return false;
  }

  WindowRect best_candidate;
  BoundaryCandidate best_edges[4];
  int best_score = -1;
  std::int64_t best_area = 0;
  const auto consider = [&](const BoundaryCandidates& left_candidates,
                            const BoundaryCandidates& right_candidates,
                            const BoundaryCandidates& top_candidates,
                            const BoundaryCandidates& bottom_candidates,
                            int minimum_width, int minimum_height,
                            bool compact_candidate)
  {
    for (std::size_t left_index = 0;
         left_index < left_candidates.count; ++left_index)
    {
      const BoundaryCandidate& detected_left =
          left_candidates.values.at(left_index);
      for (std::size_t right_index = 0;
           right_index < right_candidates.count; ++right_index)
      {
        const BoundaryCandidate& detected_right =
            right_candidates.values.at(right_index);
        for (std::size_t top_index = 0;
             top_index < top_candidates.count; ++top_index)
        {
          const BoundaryCandidate& detected_top =
              top_candidates.values.at(top_index);
          for (std::size_t bottom_index = 0;
               bottom_index < bottom_candidates.count; ++bottom_index)
          {
            const BoundaryCandidate& detected_bottom =
                bottom_candidates.values.at(bottom_index);
            const WindowRect candidate{
                detected_left.coordinate, detected_top.coordinate,
                detected_right.coordinate, detected_bottom.coordinate};
            if (candidate.width() < minimum_width ||
                candidate.height() < minimum_height ||
                (compact_candidate &&
                 (candidate.width() > kMaximumCompactRegionWidth ||
                  candidate.height() > kMaximumCompactRegionHeight)))
            {
              continue;
            }

            BoundaryCandidate left = detected_left;
            BoundaryCandidate right = detected_right;
            BoundaryCandidate top = detected_top;
            BoundaryCandidate bottom = detected_bottom;
            if (compact_candidate)
            {
              left.coverage = verticalEdgeCoverage(
                  background, image_screen_rect, available_bounds,
                  left.coordinate, candidate.top, candidate.bottom,
                  kWeakEdgeColorDifference, kMinimumCompactSamples);
              right.coverage = verticalEdgeCoverage(
                  background, image_screen_rect, available_bounds,
                  right.coordinate, candidate.top, candidate.bottom,
                  kWeakEdgeColorDifference, kMinimumCompactSamples);
              top.coverage = horizontalEdgeCoverage(
                  background, image_screen_rect, available_bounds,
                  top.coordinate, candidate.left, candidate.right,
                  kWeakEdgeColorDifference, kMinimumCompactSamples);
              bottom.coverage = horizontalEdgeCoverage(
                  background, image_screen_rect, available_bounds,
                  bottom.coordinate, candidate.left, candidate.right,
                  kWeakEdgeColorDifference, kMinimumCompactSamples);
              left.confirmed =
                  left.coverage >= kRequiredEdgeCoveragePercent;
              right.confirmed =
                  right.coverage >= kRequiredEdgeCoveragePercent;
              top.confirmed = top.coverage >= kRequiredEdgeCoveragePercent;
              bottom.confirmed =
                  bottom.coverage >= kRequiredEdgeCoveragePercent;
            }
            const int edge_count = static_cast<int>(left.confirmed) +
                                   static_cast<int>(right.confirmed) +
                                   static_cast<int>(top.confirmed) +
                                   static_cast<int>(bottom.confirmed);
            const bool has_horizontal_pair = top.confirmed && bottom.confirmed;
            const bool has_vertical_pair = left.confirmed && right.confirmed;
            const bool spans_owner_width =
                !left.confirmed && !right.confirmed &&
                left.coordinate == owner_client_rect.left &&
                right.coordinate == owner_client_rect.right;
            const bool spans_owner_height =
                !top.confirmed && !bottom.confirmed &&
                top.coordinate == owner_client_rect.top &&
                bottom.coordinate == owner_client_rect.bottom;
            // Standard mode accepts a full-width row or full-height sidebar
            // with one real pair of opposite boundaries. Both omitted sides
            // must be exactly anchored to Owner, so texture strokes and a
            // uniform background still have no valid rectangle evidence.
            const bool has_owner_anchored_boundary_pair =
                (has_horizontal_pair && spans_owner_width) ||
                (has_vertical_pair && spans_owner_height);
            const bool requires_complete_boundaries =
                compact_candidate ||
                policy == VisualRegionSearchPolicy::RequireCompleteBoundaries;
            const bool workbench_incomplete_candidate_is_too_large =
                policy ==
                    VisualRegionSearchPolicy::RejectLargeIncompleteBoundaries &&
                edge_count != 4 &&
                static_cast<std::int64_t>(candidate.width()) * 100 >=
                    static_cast<std::int64_t>(owner_client_rect.width()) *
                        kWorkbenchIncompleteMaximumOwnerSpanPercent;
            if ((requires_complete_boundaries && edge_count != 4) ||
                workbench_incomplete_candidate_is_too_large ||
                (!requires_complete_boundaries && edge_count < 3 &&
                 !has_owner_anchored_boundary_pair) ||
                (!has_horizontal_pair && !has_vertical_pair) ||
                !missingBoundaryIsAttachedToOwner(left, right, top, bottom,
                                                  owner_client_rect) ||
                !contains(candidate, screen_point) ||
                areaOf(candidate) * 100 >=
                    areaOf(owner_client_rect) * kMaximumRegionPercent)
            {
              continue;
            }

            const int score =
                rectangleEvidenceScore(left, right, top, bottom);
            const std::int64_t area = areaOf(candidate);
            if (score > best_score ||
                (score == best_score &&
                 (best_area == 0 || area < best_area)))
            {
              best_candidate = candidate;
              best_edges[0] = left;
              best_edges[1] = right;
              best_edges[2] = top;
              best_edges[3] = bottom;
              best_score = score;
              best_area = area;
            }
          }
        }
      }
    }
  };

  // Small controls cannot cover enough of the regular 192-pixel probe span
  // to seed the legacy rectangle search. Probe a narrow cross around the
  // pointer first, then validate all four edges against the candidate's real
  // dimensions. Requiring a complete rectangle keeps glyph strokes and image
  // texture from becoming compact candidates when the weak threshold is used.
  const int compact_sample_top =
      (std::max)(available_bounds.top,
                 static_cast<int>(screen_point.y) - kCompactProbeHalfExtent);
  const int compact_sample_bottom =
      (std::min)(available_bounds.bottom,
                 static_cast<int>(screen_point.y) + kCompactProbeHalfExtent +
                     1);
  const int compact_sample_left =
      (std::max)(available_bounds.left,
                 static_cast<int>(screen_point.x) - kCompactProbeHalfExtent);
  const int compact_sample_right =
      (std::min)(available_bounds.right,
                 static_cast<int>(screen_point.x) + kCompactProbeHalfExtent +
                     1);
  const BoundaryCandidates compact_left = collectVerticalBoundaries(
      background, image_screen_rect, available_bounds, screen_point,
      compact_sample_top, compact_sample_bottom, true,
      kWeakEdgeColorDifference, kMinimumCompactSamples);
  const BoundaryCandidates compact_right = collectVerticalBoundaries(
      background, image_screen_rect, available_bounds, screen_point,
      compact_sample_top, compact_sample_bottom, false,
      kWeakEdgeColorDifference, kMinimumCompactSamples);
  const BoundaryCandidates compact_top = collectHorizontalBoundaries(
      background, image_screen_rect, available_bounds, screen_point,
      compact_sample_left, compact_sample_right, true,
      kWeakEdgeColorDifference, kMinimumCompactSamples);
  const BoundaryCandidates compact_bottom = collectHorizontalBoundaries(
      background, image_screen_rect, available_bounds, screen_point,
      compact_sample_left, compact_sample_right, false,
      kWeakEdgeColorDifference, kMinimumCompactSamples);
  consider(compact_left, compact_right, compact_top, compact_bottom,
           kMinimumCompactRegionWidth, kMinimumCompactRegionHeight, true);

  const BoundaryCandidates local_left = collectVerticalBoundaries(
      background, image_screen_rect, available_bounds, screen_point,
      local_sample_bounds.top, local_sample_bounds.bottom, true);
  const BoundaryCandidates local_right = collectVerticalBoundaries(
      background, image_screen_rect, available_bounds, screen_point,
      local_sample_bounds.top, local_sample_bounds.bottom, false);
  for (std::size_t left_index = 0; left_index < local_left.count; ++left_index)
  {
    for (std::size_t right_index = 0; right_index < local_right.count;
         ++right_index)
    {
      const BoundaryCandidate& left = local_left.values.at(left_index);
      const BoundaryCandidate& right = local_right.values.at(right_index);
      if ((!left.confirmed && !right.confirmed) ||
          right.coordinate - left.coordinate < kMinimumRegionWidth)
      {
        continue;
      }
      const BoundaryCandidates top = collectHorizontalBoundaries(
          background, image_screen_rect, available_bounds, screen_point,
          left.coordinate, right.coordinate, true);
      const BoundaryCandidates bottom = collectHorizontalBoundaries(
          background, image_screen_rect, available_bounds, screen_point,
          left.coordinate, right.coordinate, false);
      BoundaryCandidates selected_left;
      selected_left.values.at(0) = left;
      selected_left.count = 1;
      BoundaryCandidates selected_right;
      selected_right.values.at(0) = right;
      selected_right.count = 1;
      consider(selected_left, selected_right, top, bottom,
               kMinimumRegionWidth, kMinimumRegionHeight, false);
    }
  }

  const BoundaryCandidates local_top = collectHorizontalBoundaries(
      background, image_screen_rect, available_bounds, screen_point,
      local_sample_bounds.left, local_sample_bounds.right, true);
  const BoundaryCandidates local_bottom = collectHorizontalBoundaries(
      background, image_screen_rect, available_bounds, screen_point,
      local_sample_bounds.left, local_sample_bounds.right, false);
  for (std::size_t top_index = 0; top_index < local_top.count; ++top_index)
  {
    for (std::size_t bottom_index = 0; bottom_index < local_bottom.count;
         ++bottom_index)
    {
      const BoundaryCandidate& top = local_top.values.at(top_index);
      const BoundaryCandidate& bottom = local_bottom.values.at(bottom_index);
      if ((!top.confirmed && !bottom.confirmed) ||
          bottom.coordinate - top.coordinate < kMinimumRegionHeight)
      {
        continue;
      }
      const BoundaryCandidates left = collectVerticalBoundaries(
          background, image_screen_rect, available_bounds, screen_point,
          top.coordinate, bottom.coordinate, true);
      const BoundaryCandidates right = collectVerticalBoundaries(
          background, image_screen_rect, available_bounds, screen_point,
          top.coordinate, bottom.coordinate, false);
      BoundaryCandidates selected_top;
      selected_top.values.at(0) = top;
      selected_top.count = 1;
      BoundaryCandidates selected_bottom;
      selected_bottom.values.at(0) = bottom;
      selected_bottom.count = 1;
      consider(left, right, selected_top, selected_bottom,
               kMinimumRegionWidth, kMinimumRegionHeight, false);
    }
  }

  if (best_score < 0)
  {
    if (policy ==
            VisualRegionSearchPolicy::RejectLargeIncompleteBoundaries &&
        findWorkbenchPanelByGlobalSeparators(
            background, image_screen_rect, available_bounds, screen_point,
            owner_client_rect, out, diagnostics, false))
    {
      return true;
    }
    return false;
  }

  diagnostics.candidate = best_candidate;
  constexpr VisualRegionEdge EdgeKinds[4] = {
      VisualRegionEdge::Left, VisualRegionEdge::Right,
      VisualRegionEdge::Top, VisualRegionEdge::Bottom};
  for (std::size_t index = 0; index < std::size(best_edges); ++index)
  {
    diagnostics.edge_coverage[index] =
        static_cast<std::uint8_t>(best_edges[index].coverage);
    if (best_edges[index].confirmed)
    {
      diagnostics.edge_mask |=
          static_cast<std::uint8_t>(EdgeKinds[index]);
    }
  }
  const int confirmed_edge_count =
      static_cast<int>(best_edges[0].confirmed) +
      static_cast<int>(best_edges[1].confirmed) +
      static_cast<int>(best_edges[2].confirmed) +
      static_cast<int>(best_edges[3].confirmed);
  diagnostics.confidence = static_cast<std::uint8_t>((std::min)(
      100, best_score * 100 / (confirmed_edge_count * 300)));
  diagnostics.accepted = true;
  out = best_candidate;
  return true;
}

bool findVisualRegionCandidate(
    const Image& background, const WindowRect& image_screen_rect,
    const WindowRect& owner_client_rect, POINT screen_point,
    std::uintptr_t owner_window, SmartRegionCandidate& out,
    VisualRegionDiagnostic* diagnostics,
    VisualRegionSearchPolicy policy) noexcept
{
  out = SmartRegionCandidate{};
  VisualRegionDiagnostic visual_diagnostic;
  WindowRect visual_rect;
  if (!findVisualRegion(background, image_screen_rect, owner_client_rect,
                        screen_point, visual_rect, visual_diagnostic, policy))
  {
    return false;
  }

  SmartRegionCandidate candidate{owner_window, owner_window, visual_rect,
                                 SmartRegionKind::KnownContent};
  candidate.source = SmartRegionDiagnosticSource::Visual;
  candidate.semantic = SmartRegionSemantic::ContentSurface;
  candidate.visual_confidence = visual_diagnostic.confidence;
  if (!candidate.valid())
  {
    return false;
  }

  out = candidate;
  if (diagnostics != nullptr)
  {
    *diagnostics = visual_diagnostic;
  }
  return true;
}

}  // namespace qingying::window_detail
