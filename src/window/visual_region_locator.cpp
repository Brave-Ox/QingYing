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
constexpr int kMaximumSearchDistance = 640;
constexpr int kSampleStride = 4;
constexpr int kMinimumSamples = 8;
constexpr int kEdgeColorDifference = 36;
constexpr int kRequiredEdgeCoveragePercent = 60;
constexpr int kLocalProbeHalfExtent = 96;
constexpr std::int64_t kMaximumRegionPercent = 85;
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

int verticalEdgeCoverage(const Image& image,
                         const WindowRect& image_screen_rect,
                         const SearchBounds& bounds, int screen_x,
                         int sample_top, int sample_bottom) noexcept
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
    if (isStrongEdge(pixelAt(image, image_screen_rect, screen_x - 1, y),
                     pixelAt(image, image_screen_rect, screen_x, y)))
    {
      ++edge_samples;
    }
  }
  return samples >= kMinimumSamples ? edge_samples * 100 / samples : 0;
}

int horizontalEdgeCoverage(const Image& image,
                           const WindowRect& image_screen_rect,
                           const SearchBounds& bounds, int screen_y,
                           int sample_left, int sample_right) noexcept
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
    if (isStrongEdge(pixelAt(image, image_screen_rect, x, screen_y - 1),
                     pixelAt(image, image_screen_rect, x, screen_y)))
    {
      ++edge_samples;
    }
  }
  return samples >= kMinimumSamples ? edge_samples * 100 / samples : 0;
}

int colorRangeConsistency(const Image& image,
                          const WindowRect& image_screen_rect,
                          bool vertical, int fixed_coordinate, int begin,
                          int end) noexcept
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
  if (samples < kMinimumSamples)
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
    int sample_bottom, bool search_left) noexcept
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
        image, image_screen_rect, bounds, x, sample_top, sample_bottom);
    const bool strong = coverage >= kRequiredEdgeCoveragePercent;
    if (strong && !inside_edge)
    {
      const int exterior_x = search_left ? x - kExteriorSampleOffset
                                         : x + kExteriorSampleOffset;
      const int consistency =
          exterior_x > bounds.left && exterior_x < bounds.right
              ? colorRangeConsistency(image, image_screen_rect, true,
                                      exterior_x, sample_top, sample_bottom)
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
    int sample_right, bool search_top) noexcept
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
        image, image_screen_rect, bounds, y, sample_left, sample_right);
    const bool strong = coverage >= kRequiredEdgeCoveragePercent;
    if (strong && !inside_edge)
    {
      const int exterior_y = search_top ? y - kExteriorSampleOffset
                                        : y + kExteriorSampleOffset;
      const int consistency =
          exterior_y > bounds.top && exterior_y < bounds.bottom
              ? colorRangeConsistency(image, image_screen_rect, false,
                                      exterior_y, sample_left, sample_right)
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
    const SearchBounds& bounds) noexcept
{
  return (left.confirmed || left.coordinate == bounds.left) &&
         (right.confirmed || right.coordinate == bounds.right) &&
         (top.confirmed || top.coordinate == bounds.top) &&
         (bottom.confirmed || bottom.coordinate == bounds.bottom);
}

int rectangleEvidenceScore(const BoundaryCandidate& left,
                           const BoundaryCandidate& right,
                           const BoundaryCandidate& top,
                           const BoundaryCandidate& bottom) noexcept
{
  return boundaryQuality(left) + boundaryQuality(right) +
         boundaryQuality(top) + boundaryQuality(bottom);
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

  WindowRect best_candidate;
  BoundaryCandidate best_edges[4];
  int best_score = -1;
  std::int64_t best_area = 0;
  const auto consider = [&](const BoundaryCandidates& left_candidates,
                            const BoundaryCandidates& right_candidates,
                            const BoundaryCandidates& top_candidates,
                            const BoundaryCandidates& bottom_candidates)
  {
    for (std::size_t left_index = 0;
         left_index < left_candidates.count; ++left_index)
    {
      const BoundaryCandidate& left =
          left_candidates.values.at(left_index);
      for (std::size_t right_index = 0;
           right_index < right_candidates.count; ++right_index)
      {
        const BoundaryCandidate& right =
            right_candidates.values.at(right_index);
        for (std::size_t top_index = 0;
             top_index < top_candidates.count; ++top_index)
        {
          const BoundaryCandidate& top = top_candidates.values.at(top_index);
          for (std::size_t bottom_index = 0;
               bottom_index < bottom_candidates.count; ++bottom_index)
          {
            const BoundaryCandidate& bottom =
                bottom_candidates.values.at(bottom_index);
            const WindowRect candidate{left.coordinate, top.coordinate,
                                       right.coordinate, bottom.coordinate};
            const int edge_count = static_cast<int>(left.confirmed) +
                                   static_cast<int>(right.confirmed) +
                                   static_cast<int>(top.confirmed) +
                                   static_cast<int>(bottom.confirmed);
            const bool has_horizontal_pair = top.confirmed && bottom.confirmed;
            const bool has_vertical_pair = left.confirmed && right.confirmed;
            if (edge_count < 3 ||
                (!has_horizontal_pair && !has_vertical_pair) ||
                !missingBoundaryIsAttachedToOwner(left, right, top, bottom,
                                                  bounds) ||
                candidate.width() < kMinimumRegionWidth ||
                candidate.height() < kMinimumRegionHeight ||
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

  const BoundaryCandidates local_left = collectVerticalBoundaries(
      background, image_screen_rect, bounds, screen_point, local_bounds.top,
      local_bounds.bottom, true);
  const BoundaryCandidates local_right = collectVerticalBoundaries(
      background, image_screen_rect, bounds, screen_point, local_bounds.top,
      local_bounds.bottom, false);
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
          background, image_screen_rect, bounds, screen_point,
          left.coordinate, right.coordinate, true);
      const BoundaryCandidates bottom = collectHorizontalBoundaries(
          background, image_screen_rect, bounds, screen_point,
          left.coordinate, right.coordinate, false);
      BoundaryCandidates selected_left;
      selected_left.values.at(0) = left;
      selected_left.count = 1;
      BoundaryCandidates selected_right;
      selected_right.values.at(0) = right;
      selected_right.count = 1;
      consider(selected_left, selected_right, top, bottom);
    }
  }

  const BoundaryCandidates local_top = collectHorizontalBoundaries(
      background, image_screen_rect, bounds, screen_point, local_bounds.left,
      local_bounds.right, true);
  const BoundaryCandidates local_bottom = collectHorizontalBoundaries(
      background, image_screen_rect, bounds, screen_point, local_bounds.left,
      local_bounds.right, false);
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
          background, image_screen_rect, bounds, screen_point, top.coordinate,
          bottom.coordinate, true);
      const BoundaryCandidates right = collectVerticalBoundaries(
          background, image_screen_rect, bounds, screen_point, top.coordinate,
          bottom.coordinate, false);
      BoundaryCandidates selected_top;
      selected_top.values.at(0) = top;
      selected_top.count = 1;
      BoundaryCandidates selected_bottom;
      selected_bottom.values.at(0) = bottom;
      selected_bottom.count = 1;
      consider(left, right, selected_top, selected_bottom);
    }
  }

  if (best_score < 0)
  {
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
    VisualRegionDiagnostic* diagnostics) noexcept
{
  out = SmartRegionCandidate{};
  VisualRegionDiagnostic visual_diagnostic;
  WindowRect visual_rect;
  if (!findVisualRegion(background, image_screen_rect, owner_client_rect,
                        screen_point, visual_rect, visual_diagnostic))
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
