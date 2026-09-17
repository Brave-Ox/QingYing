#include "qingying/window/smart_region_detector.hpp"

#include <algorithm>
#include <cstdint>

#include <Windows.h>

#include "known_content_locator.hpp"
#include "msaa_region_locator.hpp"
#include "uia_region_locator.hpp"
#include "visual_region_locator.hpp"

namespace qingying {

namespace {

constexpr int kMinimumCandidateWidth = 16;
constexpr int kMinimumCandidateHeight = 16;
constexpr int kMinimumCompactUiaControlWidth = 12;
constexpr int kMinimumCompactUiaControlHeight = 12;
constexpr std::uint8_t kMinimumVisualConfidence = 70;
constexpr std::int64_t kLargeGenericCandidatePercent = 85;
constexpr int kSourceScoreUia = 1200;
constexpr int kSourceScoreMsaa = 1100;
constexpr int kSourceScoreVisual = 1000;
constexpr int kSourceScoreKnownContent = 900;
constexpr int kSourceScoreClientArea = 200;
constexpr int kSourceScoreWindow = 100;
// 鼠标下可独立操作的控件优先于其所在的泛化内容容器。
constexpr int kSemanticScoreActionable = 1000;
constexpr int kSemanticScoreContent = 450;
constexpr int kMaximumPointerScore = 100;
constexpr int kMaximumHierarchyScore = 300;
constexpr int kHierarchyStepScore = 100;
constexpr int kNamedUiaActionableQualityScore = 250;
constexpr int kPatternUiaActionableQualityScore = 200;
constexpr int kUnnamedUiaActionableQualityScore = 50;
constexpr int kGenericUiaContainerQualityScore = 25;
constexpr std::uint8_t kMinimumWorkbenchVisualConfidence = 45;
constexpr int kVisualCacheCellSize = 16;
constexpr int kVisualNegativeCacheCellSize = 48;
constexpr int kBrowserWideVisualCacheCellWidth = 48;
constexpr int kBrowserWideVisualCacheCellHeight = 16;
constexpr std::int64_t kVisualCacheLocalCandidateMaximumOwnerSpanPercent =
    75;

int visualCacheCellFor(int coordinate, int cell_size) noexcept
{
  const std::int64_t value = coordinate;
  if (value >= 0)
  {
    return static_cast<int>(value / cell_size);
  }
  return static_cast<int>(
      -((-value + cell_size - 1) / cell_size));
}

bool isLocalVisualCacheCandidate(const SmartRegionCandidate& candidate,
                                 const WindowRect& owner_rect) noexcept
{
  if (!candidate.valid() || owner_rect.empty() ||
      candidate.rect.left < owner_rect.left ||
      candidate.rect.top < owner_rect.top ||
      candidate.rect.right > owner_rect.right ||
      candidate.rect.bottom > owner_rect.bottom)
  {
    return false;
  }
  return static_cast<std::int64_t>(candidate.rect.width()) * 100 <
             static_cast<std::int64_t>(owner_rect.width()) *
                 kVisualCacheLocalCandidateMaximumOwnerSpanPercent &&
         static_cast<std::int64_t>(candidate.rect.height()) * 100 <
             static_cast<std::int64_t>(owner_rect.height()) *
                 kVisualCacheLocalCandidateMaximumOwnerSpanPercent;
}

bool isBrowserWideVisualFallbackCandidate(
    const SmartRegionCandidate& candidate,
    const WindowRect& owner_rect) noexcept
{
  return candidate.valid() &&
         candidate.source == SmartRegionDiagnosticSource::Visual &&
         candidate.semantic == SmartRegionSemantic::ContentSurface &&
         !isLocalVisualCacheCandidate(candidate, owner_rect);
}

bool areEquivalentBrowserWideVisualFallbacks(
    const SmartRegionCandidate& left, const SmartRegionCandidate& right,
    POINT screen_point) noexcept
{
  if (left.owner_window != right.owner_window ||
      left.target_window != right.target_window ||
      left.source != SmartRegionDiagnosticSource::Visual ||
      right.source != SmartRegionDiagnosticSource::Visual ||
      left.semantic != SmartRegionSemantic::ContentSurface ||
      right.semantic != SmartRegionSemantic::ContentSurface ||
      !left.contains(screen_point.x, screen_point.y) ||
      !right.contains(screen_point.x, screen_point.y) ||
      left.rect.left != right.rect.left || left.rect.right != right.rect.right)
  {
    return false;
  }
  const int overlap_top = (std::max)(left.rect.top, right.rect.top);
  const int overlap_bottom = (std::min)(left.rect.bottom, right.rect.bottom);
  const int overlap_height = overlap_bottom - overlap_top;
  const int minimum_height =
      (std::min)(left.rect.height(), right.rect.height());
  return overlap_height > 0 && minimum_height > 0 &&
         static_cast<std::int64_t>(overlap_height) * 100 >=
             static_cast<std::int64_t>(minimum_height) * 75;
}

bool rectanglesEqual(const WindowRect& left,
                     const WindowRect& right) noexcept
{
  return left.left == right.left && left.top == right.top &&
         left.right == right.right && left.bottom == right.bottom;
}

struct CandidateScoreBreakdown
{
  int source{0};
  int semantic{0};
  int pointer{0};
  int area{0};
  int quality{0};
  int boundary{0};
  int hierarchy{0};

  int total() const noexcept
  {
    return source + semantic + pointer + area + quality + boundary +
           hierarchy;
  }
};

bool candidatesEqual(const SmartRegionCandidate& left,
                     const SmartRegionCandidate& right) noexcept
{
  return left.owner_window == right.owner_window &&
         left.target_window == right.target_window &&
         left.rect.left == right.rect.left && left.rect.top == right.rect.top &&
         left.rect.right == right.rect.right &&
         left.rect.bottom == right.rect.bottom && left.kind == right.kind;
}

bool isDetailedCandidate(const SmartRegionCandidate& candidate) noexcept
{
  return candidate.valid() && candidate.kind == SmartRegionKind::KnownContent &&
         candidate.semantic != SmartRegionSemantic::Fallback;
}

bool isAccessibilityActionableCandidate(
    const SmartRegionCandidate& candidate) noexcept
{
  const bool accessibility_source =
      candidate.source == SmartRegionDiagnosticSource::Uia ||
      candidate.source == SmartRegionDiagnosticSource::Msaa;
  return candidate.valid() && accessibility_source &&
         candidate.semantic == SmartRegionSemantic::ActionableControl;
}

bool getRootClientScreenRect(HWND root_window, WindowRect& out) noexcept
{
  RECT client{};
  if (root_window == nullptr || !GetClientRect(root_window, &client)) {
    return false;
  }

  POINT top_left{client.left, client.top};
  POINT bottom_right{client.right, client.bottom};
  if (!ClientToScreen(root_window, &top_left) ||
      !ClientToScreen(root_window, &bottom_right) ||
      bottom_right.x <= top_left.x || bottom_right.y <= top_left.y) {
    return false;
  }

  out = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  return true;
}

SmartRegionCandidate makeCandidate(HWND owner_window, HWND target_window,
                                   const WindowRect& rect,
                                   SmartRegionKind kind,
                                   SmartRegionDiagnosticSource source,
                                   SmartRegionSemantic semantic) noexcept
{
  SmartRegionCandidate candidate{
      reinterpret_cast<std::uintptr_t>(owner_window),
      reinterpret_cast<std::uintptr_t>(target_window), rect, kind};
  candidate.source = source;
  candidate.semantic = semantic;
  return candidate;
}

SmartRegionDiagnosticSource diagnosticSourceFor(
    const SmartRegionCandidate& candidate) noexcept
{
  if (candidate.source != SmartRegionDiagnosticSource::None) {
    return candidate.source;
  }
  switch (candidate.kind) {
    case SmartRegionKind::KnownContent:
      return SmartRegionDiagnosticSource::KnownContent;
    case SmartRegionKind::ClientArea:
      return SmartRegionDiagnosticSource::ClientArea;
    case SmartRegionKind::Window:
      return SmartRegionDiagnosticSource::Window;
    case SmartRegionKind::None:
      break;
  }
  return SmartRegionDiagnosticSource::None;
}

class ScopedProcessHandle
{
 public:
  explicit ScopedProcessHandle(HANDLE handle) noexcept : m_handle(handle)
  {
  }

  ~ScopedProcessHandle()
  {
    if (m_handle != nullptr)
    {
      static_cast<void>(CloseHandle(m_handle));
    }
  }

  ScopedProcessHandle(const ScopedProcessHandle&) = delete;
  ScopedProcessHandle& operator=(const ScopedProcessHandle&) = delete;

  HANDLE get() const noexcept
  {
    return m_handle;
  }

 private:
  HANDLE m_handle{nullptr};
};

void copyDiagnosticString(const wchar_t* source, wchar_t* destination,
                          std::size_t capacity) noexcept
{
  if (source == nullptr || destination == nullptr || capacity == 0)
  {
    return;
  }

  std::size_t index = 0;
  for (; index + 1 < capacity && source[index] != L'\0'; ++index)
  {
    destination[index] = source[index];
  }
  destination[index] = L'\0';
}

void recordWindowContext(SmartRegionDiagnosticEvent& event, HWND root_window,
                         int screen_x, int screen_y) noexcept
{
  event.root_window = reinterpret_cast<std::uintptr_t>(root_window);
  event.cursor_x = screen_x;
  event.cursor_y = screen_y;
  if (root_window == nullptr)
  {
    return;
  }

  static_cast<void>(GetClassNameW(
      root_window, event.window_class,
      static_cast<int>(SmartRegionDiagnosticWindowClassCapacity)));

  DWORD process_id = 0;
  static_cast<void>(GetWindowThreadProcessId(root_window, &process_id));
  event.process_id = process_id;
  if (process_id == 0)
  {
    return;
  }

  ScopedProcessHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                                          FALSE, process_id));
  if (process.get() == nullptr)
  {
    return;
  }

  wchar_t image_path[SmartRegionDiagnosticProcessNameCapacity]{};
  DWORD image_path_length =
      static_cast<DWORD>(SmartRegionDiagnosticProcessNameCapacity);
  if (!QueryFullProcessImageNameW(process.get(), 0, image_path,
                                  &image_path_length))
  {
    return;
  }

  const wchar_t* process_name = image_path;
  for (const wchar_t* character = image_path;
       *character != L'\0'; ++character)
  {
    if (*character == L'\\' || *character == L'/')
    {
      process_name = character + 1;
    }
  }
  copyDiagnosticString(process_name, event.process_name,
                       SmartRegionDiagnosticProcessNameCapacity);
}

void recordDetection(SmartRegionDiagnosticTrace* diagnostics,
                     const SmartRegionCandidate& candidate,
                     SmartRegionDiagnosticEvent& event,
                     std::uint64_t begin_ms) noexcept
{
  if (diagnostics == nullptr || !diagnostics->enabled()) {
    return;
  }
  event.source = diagnosticSourceFor(candidate);
  event.rect = candidate.rect;
  event.elapsed_ms = GetTickCount64() - begin_ms;
  static_cast<void>(diagnostics->record(event));
}

bool rectInside(const WindowRect& inner, const WindowRect& outer) noexcept
{
  return !inner.empty() && !outer.empty() && inner.left >= outer.left &&
         inner.top >= outer.top && inner.right <= outer.right &&
         inner.bottom <= outer.bottom;
}

std::int64_t areaOf(const WindowRect& rect) noexcept
{
  return static_cast<std::int64_t>(rect.width()) * rect.height();
}

SmartRegionCandidateRejection candidateRejection(
    const SmartRegionCandidate& candidate, int screen_x, int screen_y,
    const WindowRect& owner_rect,
    std::uint8_t minimum_visual_confidence = kMinimumVisualConfidence) noexcept
{
  if (!candidate.valid()) {
    return SmartRegionCandidateRejection::Invalid;
  }
  if (!candidate.contains(screen_x, screen_y)) {
    return SmartRegionCandidateRejection::PointerOutside;
  }
  if (!rectInside(candidate.rect, owner_rect)) {
    return SmartRegionCandidateRejection::OutsideOwner;
  }
  const bool is_compact_uia_control =
      candidate.source == SmartRegionDiagnosticSource::Uia &&
      candidate.semantic == SmartRegionSemantic::ActionableControl;
  const int minimum_width = is_compact_uia_control
                                ? kMinimumCompactUiaControlWidth
                                : kMinimumCandidateWidth;
  const int minimum_height = is_compact_uia_control
                                 ? kMinimumCompactUiaControlHeight
                                 : kMinimumCandidateHeight;
  if (candidate.rect.width() < minimum_width ||
      candidate.rect.height() < minimum_height) {
    return SmartRegionCandidateRejection::TooSmall;
  }
  if (candidate.source == SmartRegionDiagnosticSource::Visual &&
      candidate.visual_confidence < minimum_visual_confidence)
  {
    return SmartRegionCandidateRejection::LowConfidence;
  }

  const std::int64_t owner_area = areaOf(owner_rect);
  const std::int64_t candidate_area = areaOf(candidate.rect);
  const bool is_generic = candidate.semantic == SmartRegionSemantic::Unknown ||
                          candidate.semantic == SmartRegionSemantic::Fallback;
  const bool is_accessibility_content_surface =
      (candidate.source == SmartRegionDiagnosticSource::Uia ||
       candidate.source == SmartRegionDiagnosticSource::Msaa) &&
      candidate.semantic == SmartRegionSemantic::ContentSurface;
  const bool is_dynamic_generic =
      is_generic &&
      (candidate.source == SmartRegionDiagnosticSource::Uia ||
       candidate.source == SmartRegionDiagnosticSource::Msaa ||
       candidate.source == SmartRegionDiagnosticSource::Visual);
  if (owner_area > 0 &&
      (is_dynamic_generic || is_accessibility_content_surface) &&
      candidate_area * 100 >= owner_area * kLargeGenericCandidatePercent) {
    return SmartRegionCandidateRejection::GenericTooLarge;
  }
  return SmartRegionCandidateRejection::None;
}

int sourceScore(SmartRegionDiagnosticSource source) noexcept
{
  switch (source) {
    case SmartRegionDiagnosticSource::Uia:
      return kSourceScoreUia;
    case SmartRegionDiagnosticSource::Msaa:
      return kSourceScoreMsaa;
    case SmartRegionDiagnosticSource::KnownContent:
      return kSourceScoreKnownContent;
    case SmartRegionDiagnosticSource::Visual:
      return kSourceScoreVisual;
    case SmartRegionDiagnosticSource::ClientArea:
      return kSourceScoreClientArea;
    case SmartRegionDiagnosticSource::Window:
      return kSourceScoreWindow;
    case SmartRegionDiagnosticSource::None:
      break;
  }
  return 0;
}

int semanticScore(SmartRegionSemantic semantic) noexcept
{
  switch (semantic) {
    case SmartRegionSemantic::ActionableControl:
      return kSemanticScoreActionable;
    case SmartRegionSemantic::ContentSurface:
      return kSemanticScoreContent;
    case SmartRegionSemantic::Unknown:
    case SmartRegionSemantic::Fallback:
      break;
  }
  return 0;
}

int pointerScore(const SmartRegionCandidate& candidate, int screen_x,
                 int screen_y) noexcept
{
  const int distance_left = screen_x - candidate.rect.left;
  const int distance_right = candidate.rect.right - 1 - screen_x;
  const int distance_top = screen_y - candidate.rect.top;
  const int distance_bottom = candidate.rect.bottom - 1 - screen_y;
  const int edge_distance = (std::min)(
      (std::min)(distance_left, distance_right),
      (std::min)(distance_top, distance_bottom));
  const int minimum_dimension =
      (std::min)(candidate.rect.width(), candidate.rect.height());
  if (edge_distance <= 0 || minimum_dimension <= 0)
  {
    return 0;
  }
  const std::int64_t scaled_score =
      static_cast<std::int64_t>(edge_distance) * kMaximumPointerScore * 2 /
      minimum_dimension;
  return static_cast<int>((std::min)(
      static_cast<std::int64_t>(kMaximumPointerScore), scaled_score));
}

int areaScore(const SmartRegionCandidate& candidate,
              const WindowRect& owner_rect) noexcept
{
  // 独立控件的可操作语义已经在 semantic/quality 中体现；不再因面积小
  // 额外加分，避免无名小子控件仅凭面积抢占父控件。
  if (candidate.semantic == SmartRegionSemantic::ActionableControl)
  {
    return 0;
  }
  const std::int64_t owner_area = areaOf(owner_rect);
  const std::int64_t candidate_area = areaOf(candidate.rect);
  if (owner_area <= 0 || candidate_area <= 0)
  {
    return 0;
  }
  const long double coverage_percent =
      static_cast<long double>(candidate_area) * 100.0L /
      static_cast<long double>(owner_area);
  if (coverage_percent <= 5)
  {
    return 700;
  }
  if (coverage_percent <= 20)
  {
    return 600;
  }
  if (coverage_percent <= 50)
  {
    return 350;
  }
  if (coverage_percent <= 75)
  {
    return 150;
  }
  return 0;
}

int qualityScore(const SmartRegionCandidate& candidate) noexcept
{
  if (candidate.source != SmartRegionDiagnosticSource::Uia ||
      !candidate.uia_metadata.available)
  {
    return 0;
  }
  switch (candidate.uia_metadata.quality)
  {
    case SmartRegionUiaQuality::NamedActionable:
      return kNamedUiaActionableQualityScore;
    case SmartRegionUiaQuality::PatternActionable:
      return kPatternUiaActionableQualityScore;
    case SmartRegionUiaQuality::UnnamedActionable:
      return kUnnamedUiaActionableQualityScore;
    case SmartRegionUiaQuality::GenericContainer:
      return kGenericUiaContainerQualityScore;
    case SmartRegionUiaQuality::None:
    case SmartRegionUiaQuality::Disabled:
    case SmartRegionUiaQuality::ContentSurface:
      return 0;
  }
  return 0;
}

int boundaryScore(const SmartRegionCandidate& candidate) noexcept
{
  if (candidate.source == SmartRegionDiagnosticSource::Visual)
  {
    return static_cast<int>(candidate.visual_confidence) * 8;
  }
  if (candidate.source == SmartRegionDiagnosticSource::Uia ||
      candidate.source == SmartRegionDiagnosticSource::Msaa)
  {
    return candidate.semantic == SmartRegionSemantic::ActionableControl
               ? 300
               : 150;
  }
  return candidate.source == SmartRegionDiagnosticSource::KnownContent ? 100
                                                                        : 0;
}

int hierarchyScore(const SmartRegionCandidate& candidate,
                   const SmartRegionCandidate* candidates,
                   std::size_t candidate_count, int screen_x,
                   int screen_y) noexcept
{
  if (candidate.semantic == SmartRegionSemantic::Fallback)
  {
    return 0;
  }
  int score = 0;
  const std::int64_t candidate_area = areaOf(candidate.rect);
  for (std::size_t index = 0; index < candidate_count; ++index)
  {
    const SmartRegionCandidate& container = candidates[index];
    if (!container.valid() || !container.contains(screen_x, screen_y) ||
        container.semantic == SmartRegionSemantic::Fallback ||
        areaOf(container.rect) <= candidate_area ||
        !rectInside(candidate.rect, container.rect))
    {
      continue;
    }
    score = (std::min)(kMaximumHierarchyScore,
                       score + kHierarchyStepScore);
  }
  return score;
}

CandidateScoreBreakdown candidateScore(
    const SmartRegionCandidate& candidate,
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    int screen_x, int screen_y, const WindowRect& owner_rect) noexcept
{
  CandidateScoreBreakdown score;
  score.source = sourceScore(diagnosticSourceFor(candidate));
  score.semantic = semanticScore(candidate.semantic);
  score.pointer = pointerScore(candidate, screen_x, screen_y);
  score.area = areaScore(candidate, owner_rect);
  score.quality = qualityScore(candidate);
  score.boundary = boundaryScore(candidate);
  score.hierarchy = hierarchyScore(candidate, candidates, candidate_count,
                                   screen_x, screen_y);
  return score;
}

bool rectanglesAreNearDuplicates(const WindowRect& left,
                                 const WindowRect& right) noexcept
{
  const WindowRect overlap{
      (std::max)(left.left, right.left),
      (std::max)(left.top, right.top),
      (std::min)(left.right, right.right),
      (std::min)(left.bottom, right.bottom)};
  const std::int64_t overlap_area = areaOf(overlap);
  const std::int64_t left_area = areaOf(left);
  const std::int64_t right_area = areaOf(right);
  const std::int64_t smaller_area = (std::min)(left_area, right_area);
  const std::int64_t larger_area = (std::max)(left_area, right_area);
  if (overlap_area <= 0 || smaller_area <= 0)
  {
    return false;
  }
  const long double overlap_ratio =
      static_cast<long double>(overlap_area) /
      static_cast<long double>(smaller_area);
  const long double area_ratio = static_cast<long double>(larger_area) /
                                 static_cast<long double>(smaller_area);
  return overlap_ratio >= 0.90L && area_ratio <= 1.10L;
}

bool selectBestInternal(const SmartRegionCandidate* candidates,
                        std::size_t candidate_count, int screen_x,
                        int screen_y, const WindowRect& owner_rect,
                        SmartRegionCandidate& out,
                        SmartRegionDiagnosticEvent* diagnostics,
                        std::uint8_t minimum_visual_confidence) noexcept
{
  out = SmartRegionCandidate{};
  if (diagnostics != nullptr) {
    diagnostics->candidate_count = 0;
    for (std::size_t index = 0;
         index < SmartRegionDiagnosticMaxCandidates; ++index) {
      diagnostics->candidates[index] = SmartRegionCandidateDiagnostic{};
    }
  }
  if (candidates == nullptr || candidate_count == 0 || owner_rect.empty()) {
    return false;
  }

  const std::size_t processing_count =
      (std::min)(candidate_count, SmartRegionMaxCandidates);
  const std::size_t diagnostic_count = processing_count;
  if (diagnostics != nullptr) {
    diagnostics->candidate_count = diagnostic_count;
  }

  bool duplicate[SmartRegionMaxCandidates]{};
  for (std::size_t left_index = 0; left_index < processing_count;
       ++left_index)
  {
    if (duplicate[left_index] ||
        candidateRejection(candidates[left_index], screen_x, screen_y,
                           owner_rect, minimum_visual_confidence) !=
        SmartRegionCandidateRejection::None)
    {
      continue;
    }
    for (std::size_t right_index = left_index + 1;
         right_index < processing_count; ++right_index)
    {
      if (duplicate[right_index] ||
          candidateRejection(candidates[right_index], screen_x, screen_y,
                             owner_rect, minimum_visual_confidence) !=
              SmartRegionCandidateRejection::None ||
          !rectanglesAreNearDuplicates(candidates[left_index].rect,
                                       candidates[right_index].rect))
      {
        continue;
      }
      const bool left_is_actionable =
          isAccessibilityActionableCandidate(candidates[left_index]);
      const bool right_is_actionable =
          isAccessibilityActionableCandidate(candidates[right_index]);
      if (left_is_actionable != right_is_actionable)
      {
        duplicate[left_is_actionable ? right_index : left_index] = true;
        if (right_is_actionable)
        {
          break;
        }
        continue;
      }
      const CandidateScoreBreakdown left_score = candidateScore(
          candidates[left_index], candidates, processing_count, screen_x,
          screen_y, owner_rect);
      const CandidateScoreBreakdown right_score = candidateScore(
          candidates[right_index], candidates, processing_count, screen_x,
          screen_y, owner_rect);
      if (right_score.total() > left_score.total())
      {
        duplicate[left_index] = true;
        break;
      }
      duplicate[right_index] = true;
    }
  }

  bool has_accessibility_actionable = false;
  for (std::size_t index = 0; index < processing_count; ++index)
  {
    if (!duplicate[index] &&
        candidateRejection(candidates[index], screen_x, screen_y,
                           owner_rect, minimum_visual_confidence) ==
            SmartRegionCandidateRejection::None &&
        isAccessibilityActionableCandidate(candidates[index]))
    {
      has_accessibility_actionable = true;
      break;
    }
  }

  int best_score = -1;
  std::int64_t best_area = 0;
  std::size_t best_index = processing_count;
  for (std::size_t index = 0; index < processing_count; ++index) {
    const SmartRegionCandidate& candidate = candidates[index];
    SmartRegionCandidateRejection rejection =
        candidateRejection(candidate, screen_x, screen_y, owner_rect,
                           minimum_visual_confidence);
    if (rejection == SmartRegionCandidateRejection::None && duplicate[index])
    {
      rejection = SmartRegionCandidateRejection::Duplicate;
    }
    if (diagnostics != nullptr && index < diagnostic_count) {
      SmartRegionCandidateDiagnostic& diagnostic =
          diagnostics->candidates[index];
      diagnostic.candidate = candidate;
      diagnostic.area = areaOf(candidate.rect);
      const std::int64_t owner_area = areaOf(owner_rect);
      if (diagnostic.area > 0 && owner_area > 0)
      {
        const long double coverage =
            static_cast<long double>(diagnostic.area) * 100.0L /
            static_cast<long double>(owner_area);
        diagnostic.owner_coverage_percent = static_cast<std::uint8_t>(
            (std::min)(100.0L, coverage));
      }
      diagnostic.rejection = rejection;
    }
    if (rejection != SmartRegionCandidateRejection::None) {
      continue;
    }
    if (has_accessibility_actionable &&
        !isAccessibilityActionableCandidate(candidate))
    {
      continue;
    }

    const CandidateScoreBreakdown score = candidateScore(
        candidate, candidates, processing_count, screen_x, screen_y,
        owner_rect);
    if (diagnostics != nullptr && index < diagnostic_count) {
      SmartRegionCandidateDiagnostic& diagnostic =
          diagnostics->candidates[index];
      diagnostic.score = score.total();
      diagnostic.source_score = score.source;
      diagnostic.semantic_score = score.semantic;
      diagnostic.pointer_score = score.pointer;
      diagnostic.area_score = score.area;
      diagnostic.quality_score = score.quality;
      diagnostic.boundary_score = score.boundary;
      diagnostic.hierarchy_score = score.hierarchy;
    }
    const std::int64_t area = areaOf(candidate.rect);
    if (score.total() > best_score ||
        (score.total() == best_score &&
         (best_area == 0 || area < best_area)) ||
        (score.total() == best_score && area == best_area &&
         candidate.target_window < out.target_window)) {
      out = candidate;
      best_score = score.total();
      best_area = area;
      best_index = index;
    }
  }

  if (diagnostics != nullptr) {
    for (std::size_t index = 0; index < diagnostic_count; ++index) {
      SmartRegionCandidateDiagnostic& diagnostic =
          diagnostics->candidates[index];
      if (index == best_index) {
        diagnostic.selected = true;
      } else if (diagnostic.rejection == SmartRegionCandidateRejection::None) {
        diagnostic.rejection = SmartRegionCandidateRejection::LowerScore;
      }
    }
  }
  return out.valid();
}

}  // namespace

bool SmartRegionCandidate::valid() const noexcept
{
  return owner_window != 0 && target_window != 0 && !rect.empty() &&
         kind != SmartRegionKind::None;
}

bool SmartRegionCandidate::contains(int screen_x, int screen_y) const noexcept
{
  return valid() && screen_x >= rect.left && screen_x < rect.right &&
         screen_y >= rect.top && screen_y < rect.bottom;
}

void SmartRegionCandidateCollection::replace(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    int screen_x, int screen_y, const WindowRect& owner_rect,
    const SmartRegionCandidate& selected,
    std::uint8_t minimum_visual_confidence) noexcept
{
  clear();
  if (candidates == nullptr || owner_rect.empty())
  {
    return;
  }

  const std::size_t input_count =
      (std::min)(candidate_count, SmartRegionMaxCandidates);
  for (std::size_t input_index = 0; input_index < input_count; ++input_index)
  {
    const SmartRegionCandidate& candidate = candidates[input_index];
    if (candidateRejection(candidate, screen_x, screen_y, owner_rect,
                           minimum_visual_confidence) !=
        SmartRegionCandidateRejection::None)
    {
      continue;
    }
    bool duplicate = false;
    for (std::size_t existing_index = 0; existing_index < m_count;
         ++existing_index)
    {
      if (candidate.kind == m_candidates[existing_index].kind &&
          candidate.semantic == m_candidates[existing_index].semantic &&
          rectanglesAreNearDuplicates(candidate.rect,
                                      m_candidates[existing_index].rect))
      {
        duplicate = true;
        if (candidatesEqual(candidate, selected))
        {
          m_candidates[existing_index] = candidate;
        }
        break;
      }
    }
    if (!duplicate)
    {
      m_candidates[m_count++] = candidate;
    }
  }

  const auto hierarchy_rank = [](const SmartRegionCandidate& candidate)
  {
    if (candidate.kind == SmartRegionKind::Window)
    {
      return 4;
    }
    if (candidate.kind == SmartRegionKind::ClientArea)
    {
      return 3;
    }
    if (candidate.semantic == SmartRegionSemantic::ActionableControl)
    {
      return 0;
    }
    if (candidate.uia_metadata.quality ==
        SmartRegionUiaQuality::GenericContainer)
    {
      return 1;
    }
    if (candidate.semantic == SmartRegionSemantic::ContentSurface)
    {
      return 2;
    }
    return 2;
  };
  std::sort(m_candidates, m_candidates + m_count,
            [&hierarchy_rank](const SmartRegionCandidate& left,
                              const SmartRegionCandidate& right)
            {
              const int left_rank = hierarchy_rank(left);
              const int right_rank = hierarchy_rank(right);
              if (left_rank != right_rank)
              {
                return left_rank < right_rank;
              }
              const std::int64_t left_area = areaOf(left.rect);
              const std::int64_t right_area = areaOf(right.rect);
              if (left_area != right_area)
              {
                return left_area < right_area;
              }
              return left.target_window < right.target_window;
            });
  for (std::size_t index = 0; index < m_count; ++index)
  {
    const bool same_hierarchy =
        m_candidates[index].kind == selected.kind &&
        m_candidates[index].semantic == selected.semantic;
    if (same_hierarchy &&
        (candidatesEqual(m_candidates[index], selected) ||
         rectanglesAreNearDuplicates(m_candidates[index].rect,
                                     selected.rect)))
    {
      m_current_index = index;
      break;
    }
  }
}

bool SmartRegionCandidateCollection::cycle(int direction) noexcept
{
  if (m_count < 2 || direction == 0)
  {
    return false;
  }
  if (direction > 0)
  {
    m_current_index = (m_current_index + 1) % m_count;
  }
  else
  {
    m_current_index = (m_current_index + m_count - 1) % m_count;
  }
  return true;
}

void SmartRegionCandidateCollection::clear() noexcept
{
  for (SmartRegionCandidate& candidate : m_candidates)
  {
    candidate = SmartRegionCandidate{};
  }
  m_count = 0;
  m_current_index = 0;
}

bool SmartRegionCandidateCollection::empty() const noexcept
{
  return m_count == 0;
}

std::size_t SmartRegionCandidateCollection::count() const noexcept
{
  return m_count;
}

const SmartRegionCandidate& SmartRegionCandidateCollection::current()
    const noexcept
{
  return candidateAt(m_current_index);
}

const SmartRegionCandidate& SmartRegionCandidateCollection::candidateAt(
    std::size_t index) const noexcept
{
  static const SmartRegionCandidate EmptyCandidate;
  return index < m_count ? m_candidates[index] : EmptyCandidate;
}

bool SmartRegionVisualContext::valid() const noexcept
{
  return background != nullptr && !background->empty() &&
         !image_screen_rect.empty() &&
         background->width == image_screen_rect.width() &&
         background->height == image_screen_rect.height();
}

bool SmartRegionVisualResultCache::lookup(
    std::uintptr_t root_window, std::uintptr_t background_identity,
    const WindowRect& owner_rect, int screen_x, int screen_y,
    SmartRegionCandidate& out, bool& found,
    bool allow_browser_wide_fallback) const noexcept
{
  out = SmartRegionCandidate{};
  found = false;
  if (!m_valid || root_window != m_root_window ||
      background_identity != m_background_identity ||
      !rectanglesEqual(owner_rect, m_owner_rect))
  {
    return false;
  }
  if (allow_browser_wide_fallback && m_has_browser_wide_fallback &&
      visualCacheCellFor(screen_x, kBrowserWideVisualCacheCellWidth) ==
          m_browser_wide_fallback_cell_x &&
      visualCacheCellFor(screen_y, kBrowserWideVisualCacheCellHeight) ==
          m_browser_wide_fallback_cell_y &&
      m_browser_wide_fallback.contains(screen_x, screen_y))
  {
    found = true;
    out = m_browser_wide_fallback;
    return true;
  }
  if (m_has_negative_cell &&
      visualCacheCellFor(screen_x, kVisualNegativeCacheCellSize) ==
          m_negative_cell_x &&
      visualCacheCellFor(screen_y, kVisualNegativeCacheCellSize) ==
          m_negative_cell_y)
  {
    return true;
  }
  for (std::size_t index = 0; index < m_positive_candidate_count; ++index)
  {
    const SmartRegionCandidate& candidate =
        m_positive_candidates[index];
    if (isLocalVisualCacheCandidate(candidate, owner_rect) &&
        candidate.contains(screen_x, screen_y))
    {
      found = true;
      out = candidate;
      return true;
    }
  }
  return false;
}

void SmartRegionVisualResultCache::store(
    std::uintptr_t root_window, std::uintptr_t background_identity,
    const WindowRect& owner_rect, int screen_x, int screen_y,
    const SmartRegionCandidate* candidate,
    std::uint8_t minimum_visual_confidence,
    bool allow_browser_wide_fallback) noexcept
{
  if (!m_valid || root_window != m_root_window ||
      background_identity != m_background_identity ||
      !rectanglesEqual(owner_rect, m_owner_rect))
  {
    clear();
    m_root_window = root_window;
    m_background_identity = background_identity;
    m_owner_rect = owner_rect;
    m_valid = root_window != 0 && background_identity != 0 &&
              !owner_rect.empty();
  }
  if (!m_valid)
  {
    return;
  }

  if (allow_browser_wide_fallback && candidate != nullptr &&
      isBrowserWideVisualFallbackCandidate(*candidate, owner_rect) &&
      candidate->visual_confidence >= minimum_visual_confidence)
  {
    m_browser_wide_fallback = *candidate;
    m_browser_wide_fallback_cell_x =
        visualCacheCellFor(screen_x, kBrowserWideVisualCacheCellWidth);
    m_browser_wide_fallback_cell_y =
        visualCacheCellFor(screen_y, kBrowserWideVisualCacheCellHeight);
    m_has_browser_wide_fallback = true;
    m_has_negative_cell = false;
    return;
  }

  if (candidate != nullptr && candidate->valid() &&
      (candidate->source != SmartRegionDiagnosticSource::Visual ||
       candidate->visual_confidence >= minimum_visual_confidence))
  {
    for (std::size_t index = 0; index < m_positive_candidate_count; ++index)
    {
      if (rectanglesEqual(m_positive_candidates[index].rect,
                          candidate->rect))
      {
        m_positive_candidates[index] = *candidate;
        return;
      }
    }
    const std::size_t index =
        m_positive_candidate_count < MaximumPositiveCandidates
            ? m_positive_candidate_count++
            : m_next_positive_candidate_index;
    m_positive_candidates[index] = *candidate;
    m_next_positive_candidate_index =
        (index + 1) % MaximumPositiveCandidates;
    return;
  }

  if (candidate != nullptr)
  {
    return;
  }

  m_negative_cell_x =
      visualCacheCellFor(screen_x, kVisualNegativeCacheCellSize);
  m_negative_cell_y =
      visualCacheCellFor(screen_y, kVisualNegativeCacheCellSize);
  m_has_negative_cell = true;
}

void SmartRegionVisualResultCache::clear() noexcept
{
  m_root_window = 0;
  m_background_identity = 0;
  m_owner_rect = WindowRect{};
  for (SmartRegionCandidate& candidate : m_positive_candidates)
  {
    candidate = SmartRegionCandidate{};
  }
  m_browser_wide_fallback = SmartRegionCandidate{};
  m_positive_candidate_count = 0;
  m_next_positive_candidate_index = 0;
  m_browser_wide_fallback_cell_x = 0;
  m_browser_wide_fallback_cell_y = 0;
  m_negative_cell_x = 0;
  m_negative_cell_y = 0;
  m_valid = false;
  m_has_browser_wide_fallback = false;
  m_has_negative_cell = false;
}

bool SmartRegionWindowSnapshot::valid() const noexcept
{
  return root_window != 0 && !owner_rect.empty();
}

bool SmartRegionCandidateSelector::selectBest(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    int screen_x, int screen_y, const WindowRect& owner_rect,
    SmartRegionCandidate& out) noexcept
{
  return selectBestInternal(candidates, candidate_count, screen_x, screen_y,
                            owner_rect, out, nullptr,
                            kMinimumVisualConfidence);
}

bool SmartRegionCandidateSelector::selectBest(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    int screen_x, int screen_y, const WindowRect& owner_rect,
    SmartRegionCandidate& out, SmartRegionDiagnosticEvent& diagnostics) noexcept
{
  return selectBestInternal(candidates, candidate_count, screen_x, screen_y,
                            owner_rect, out, &diagnostics,
                            kMinimumVisualConfidence);
}

bool SmartRegionCandidateSelector::selectBest(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    int screen_x, int screen_y, const WindowRect& owner_rect,
    SmartRegionCandidate& out,
    std::uint8_t minimum_visual_confidence) noexcept
{
  return selectBestInternal(candidates, candidate_count, screen_x, screen_y,
                            owner_rect, out, nullptr,
                            minimum_visual_confidence);
}

bool SmartRegionCandidateSelector::selectBest(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    int screen_x, int screen_y, const WindowRect& owner_rect,
    SmartRegionCandidate& out, SmartRegionDiagnosticEvent& diagnostics,
    std::uint8_t minimum_visual_confidence) noexcept
{
  return selectBestInternal(candidates, candidate_count, screen_x, screen_y,
                            owner_rect, out, &diagnostics,
                            minimum_visual_confidence);
}

bool SmartRegionCandidateSelector::hasValidLocalCandidate(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    int screen_x, int screen_y, const WindowRect& owner_rect,
    SmartRegionDiagnosticSource source) noexcept
{
  if (candidates == nullptr || owner_rect.empty())
  {
    return false;
  }
  for (std::size_t index = 0; index < candidate_count; ++index)
  {
    const SmartRegionCandidate& candidate = candidates[index];
    const bool local_semantic =
        candidate.semantic == SmartRegionSemantic::ActionableControl ||
        candidate.semantic == SmartRegionSemantic::ContentSurface;
    if (candidate.source == source && local_semantic &&
        candidateRejection(candidate, screen_x, screen_y, owner_rect) ==
            SmartRegionCandidateRejection::None)
    {
      return true;
    }
  }
  return false;
}

void SmartRegionDiagnosticTrace::setEnabled(bool enabled) noexcept
{
  m_enabled = enabled;
  if (!m_enabled) {
    m_has_latest_event = false;
    m_latest_event = SmartRegionDiagnosticEvent{};
  }
}

bool SmartRegionDiagnosticTrace::enabled() const noexcept
{
  return m_enabled;
}

bool SmartRegionDiagnosticTrace::record(
    const SmartRegionDiagnosticEvent& event) noexcept
{
  if (!m_enabled) {
    return false;
  }
  m_latest_event = event;
  m_has_latest_event = true;
  return true;
}

bool SmartRegionDiagnosticTrace::recordOverlayRenderElapsed(
    std::uint64_t elapsed_ms, std::uint64_t render_count) noexcept
{
  if (!m_enabled || !m_has_latest_event) {
    return false;
  }
  m_latest_event.overlay_render_ms = elapsed_ms;
  m_latest_event.overlay_render_count = render_count;
  return true;
}

bool SmartRegionDiagnosticTrace::recordOverlayRenderCount(
    std::uint64_t render_count) noexcept
{
  if (!m_enabled || !m_has_latest_event)
  {
    return false;
  }
  m_latest_event.overlay_render_count = render_count;
  return true;
}

bool SmartRegionDiagnosticTrace::recordHoverInputDelay(
    std::uint64_t delay_ms) noexcept
{
  if (!m_enabled || !m_has_latest_event)
  {
    return false;
  }
  m_latest_event.hover_input_delay_ms = delay_ms;
  return true;
}

bool SmartRegionDiagnosticTrace::recordStabilizationDelay(
    std::uint64_t delay_ms) noexcept
{
  if (!m_enabled || !m_has_latest_event) {
    return false;
  }
  m_latest_event.stabilization_delay_ms = delay_ms;
  return true;
}

bool SmartRegionDiagnosticTrace::recordHoverMotion(
    std::int64_t delta_x, std::int64_t delta_y, std::uint64_t elapsed_ms,
    bool fast) noexcept
{
  if (!m_enabled || !m_has_latest_event)
  {
    return false;
  }
  m_latest_event.hover_motion_delta_x = delta_x;
  m_latest_event.hover_motion_delta_y = delta_y;
  m_latest_event.hover_motion_elapsed_ms = elapsed_ms;
  m_latest_event.hover_motion_fast = fast;
  return true;
}

bool SmartRegionDiagnosticTrace::recordAsyncUiaResult(
    std::uint64_t request_id, std::uint64_t elapsed_ms,
    std::uint64_t age_ms, std::uint64_t poll_delay_ms, bool succeeded,
    bool msaa_attempted, bool browser_semantic_miss,
    bool cache_hit, bool suppressed_by_cooldown,
    std::size_t candidate_count, bool matches_current_request,
    bool applied, bool deferred,
    SmartRegionAsyncDeferralReason deferral_reason,
    const SmartRegionMsaaTraversalDiagnostic& msaa_diagnostic,
    const SmartRegionCandidate* candidates,
    std::size_t diagnostic_candidate_count) noexcept
{
  if (!m_enabled || !m_has_latest_event)
  {
    return false;
  }
  m_latest_event.uia_async_result_received = true;
  m_latest_event.uia_async_request_id = request_id;
  m_latest_event.uia_async_elapsed_ms = elapsed_ms;
  m_latest_event.uia_async_age_ms = age_ms;
  m_latest_event.uia_async_poll_delay_ms = poll_delay_ms;
  m_latest_event.uia_async_candidate_count = candidate_count;
  m_latest_event.uia_async_result_succeeded = succeeded;
  m_latest_event.uia_async_msaa_attempted = msaa_attempted;
  m_latest_event.uia_async_browser_semantic_miss = browser_semantic_miss;
  m_latest_event.uia_async_cache_hit = cache_hit;
  m_latest_event.uia_async_suppressed_by_cooldown = suppressed_by_cooldown;
  m_latest_event.uia_async_matches_current_request =
      matches_current_request;
  m_latest_event.uia_async_result_applied = applied;
  m_latest_event.uia_async_result_deferred = deferred;
  m_latest_event.uia_async_deferral_reason = deferral_reason;
  m_latest_event.uia_async_msaa_diagnostic = msaa_diagnostic;
  m_latest_event.uia_async_diagnostic_candidate_count = 0;
  for (std::size_t index = 0;
       index < SmartRegionDiagnosticMaxCandidates; ++index)
  {
    m_latest_event.uia_async_candidates[index] = SmartRegionCandidate{};
  }
  if (candidates != nullptr)
  {
    const std::size_t copied_count =
        (std::min)(diagnostic_candidate_count,
                   SmartRegionDiagnosticMaxCandidates);
    for (std::size_t index = 0; index < copied_count; ++index)
    {
      m_latest_event.uia_async_candidates[index] = candidates[index];
    }
    m_latest_event.uia_async_diagnostic_candidate_count = copied_count;
  }
  return true;
}

bool SmartRegionDiagnosticTrace::hasLatestEvent() const noexcept
{
  return m_has_latest_event;
}

const SmartRegionDiagnosticEvent& SmartRegionDiagnosticTrace::latestEvent()
    const noexcept
{
  return m_latest_event;
}

const wchar_t* smartRegionDiagnosticSourceName(
    SmartRegionDiagnosticSource source) noexcept
{
  switch (source) {
    case SmartRegionDiagnosticSource::Uia:
      return L"uia";
    case SmartRegionDiagnosticSource::Msaa:
      return L"msaa";
    case SmartRegionDiagnosticSource::KnownContent:
      return L"known-content";
    case SmartRegionDiagnosticSource::Visual:
      return L"visual";
    case SmartRegionDiagnosticSource::ClientArea:
      return L"client-area";
    case SmartRegionDiagnosticSource::Window:
      return L"window";
    case SmartRegionDiagnosticSource::None:
      break;
  }
  return L"none";
}

const wchar_t* smartRegionCandidateRejectionName(
    SmartRegionCandidateRejection rejection) noexcept
{
  switch (rejection) {
    case SmartRegionCandidateRejection::Invalid:
      return L"invalid";
    case SmartRegionCandidateRejection::PointerOutside:
      return L"pointer-outside";
    case SmartRegionCandidateRejection::OutsideOwner:
      return L"outside-owner";
    case SmartRegionCandidateRejection::TooSmall:
      return L"too-small";
    case SmartRegionCandidateRejection::LowConfidence:
      return L"low-confidence";
    case SmartRegionCandidateRejection::GenericTooLarge:
      return L"generic-too-large";
    case SmartRegionCandidateRejection::Duplicate:
      return L"duplicate";
    case SmartRegionCandidateRejection::LowerScore:
      return L"lower-score";
    case SmartRegionCandidateRejection::None:
      break;
  }
  return L"selected";
}

bool SmartRegionHoverStabilizer::update(
    const SmartRegionCandidate& candidate, std::uint64_t now_ms) noexcept
{
  return update(candidate, now_ms, {}, false);
}

const wchar_t* smartRegionAsyncDeferralReasonName(
    SmartRegionAsyncDeferralReason reason) noexcept
{
  switch (reason)
  {
    case SmartRegionAsyncDeferralReason::NotChromiumBrowser:
      return L"not-chromium";
    case SmartRegionAsyncDeferralReason::MotionBelowThreshold:
      return L"motion-below-threshold";
    case SmartRegionAsyncDeferralReason::FastMotion:
      return L"fast-motion";
    case SmartRegionAsyncDeferralReason::NotEvaluated:
      break;
  }
  return L"not-evaluated";
}

const wchar_t* smartRegionMsaaTraversalPathName(
    SmartRegionMsaaTraversalPath path) noexcept
{
  switch (path)
  {
    case SmartRegionMsaaTraversalPath::DirectPoint:
      return L"DirectPoint";
    case SmartRegionMsaaTraversalPath::RootHitTest:
      return L"RootHitTest";
    case SmartRegionMsaaTraversalPath::AccessibleChildren:
      return L"AccessibleChildren";
    case SmartRegionMsaaTraversalPath::None:
      break;
  }
  return L"None";
}

const wchar_t* smartRegionMsaaTraversalStopReasonName(
    SmartRegionMsaaTraversalStopReason stop_reason) noexcept
{
  switch (stop_reason)
  {
    case SmartRegionMsaaTraversalStopReason::CandidateFound:
      return L"CandidateFound";
    case SmartRegionMsaaTraversalStopReason::NoChildren:
      return L"NoChildren";
    case SmartRegionMsaaTraversalStopReason::HitTestFailed:
      return L"HitTestFailed";
    case SmartRegionMsaaTraversalStopReason::NodeBudgetExhausted:
      return L"NodeBudgetExhausted";
    case SmartRegionMsaaTraversalStopReason::TimeBudgetExhausted:
      return L"TimeBudgetExhausted";
    case SmartRegionMsaaTraversalStopReason::DepthLimitReached:
      return L"DepthLimitReached";
    case SmartRegionMsaaTraversalStopReason::NoCandidate:
      return L"NoCandidate";
    case SmartRegionMsaaTraversalStopReason::None:
      break;
  }
  return L"None";
}

const wchar_t* smartRegionMsaaFilteredNodeReasonName(
    SmartRegionMsaaFilteredNodeReason reason) noexcept
{
  switch (reason)
  {
    case SmartRegionMsaaFilteredNodeReason::MissingAccessible:
      return L"MissingAccessible";
    case SmartRegionMsaaFilteredNodeReason::RoleUnavailable:
      return L"RoleUnavailable";
    case SmartRegionMsaaFilteredNodeReason::RectUnavailable:
      return L"RectUnavailable";
    case SmartRegionMsaaFilteredNodeReason::UnknownSemantic:
      return L"UnknownSemantic";
    case SmartRegionMsaaFilteredNodeReason::OutsideOwner:
      return L"OutsideOwner";
    case SmartRegionMsaaFilteredNodeReason::WindowSizedContentSurface:
      return L"WindowSizedContentSurface";
    case SmartRegionMsaaFilteredNodeReason::InvisibleOrOffscreen:
      return L"InvisibleOrOffscreen";
    case SmartRegionMsaaFilteredNodeReason::PointerOutside:
      return L"PointerOutside";
    case SmartRegionMsaaFilteredNodeReason::None:
      break;
  }
  return L"None";
}

bool SmartRegionHoverStabilizer::update(
    const SmartRegionCandidate& candidate, std::uint64_t now_ms,
    POINT screen_point,
    bool preserve_browser_wide_visual_fallback) noexcept
{
  if (!candidate.valid()) {
    clear();
    return false;
  }

  if (!m_stable.valid()) {
    m_stable = candidate;
    m_pending = SmartRegionCandidate{};
    m_pending_since_ms = 0;
    return true;
  }

  // A lightweight window snapshot must not restart a local candidate's dwell.
  if (!isDetailedCandidate(candidate)) {
    if (isDetailedCandidate(m_pending) &&
        m_pending.contains(screen_point.x, screen_point.y))
      return update(m_pending, now_ms, screen_point,
                    preserve_browser_wide_visual_fallback);
    if (isDetailedCandidate(m_stable) &&
        m_stable.contains(screen_point.x, screen_point.y)) {
      m_pending = SmartRegionCandidate{};
      m_pending_since_ms = 0;
      return false;
    }
  }

  if (candidatesEqual(candidate, m_stable)) {
    m_pending = SmartRegionCandidate{};
    m_pending_since_ms = 0;
    return false;
  }

  if (preserve_browser_wide_visual_fallback &&
      areEquivalentBrowserWideVisualFallbacks(candidate, m_stable,
                                               screen_point))
  {
    m_pending = SmartRegionCandidate{};
    m_pending_since_ms = 0;
    return false;
  }

  if (isAccessibilityActionableCandidate(m_stable) &&
      !isAccessibilityActionableCandidate(candidate) &&
      m_stable.contains(screen_point.x, screen_point.y))
  {
    m_pending = SmartRegionCandidate{};
    m_pending_since_ms = 0;
    return false;
  }

  if (m_stable.semantic != SmartRegionSemantic::ActionableControl &&
      isAccessibilityActionableCandidate(candidate))
  {
    m_stable = candidate;
    m_pending = SmartRegionCandidate{};
    m_pending_since_ms = 0;
    return true;
  }

  if (!isDetailedCandidate(m_stable) && isDetailedCandidate(candidate))
  {
    m_stable = candidate;
    m_pending = SmartRegionCandidate{};
    m_pending_since_ms = 0;
    return true;
  }

  if (!candidatesEqual(candidate, m_pending)) {
    m_pending = candidate;
    m_pending_since_ms = now_ms;
    return false;
  }

  if (now_ms - m_pending_since_ms < CandidateSwitchDelayMs) {
    return false;
  }

  m_stable = candidate;
  m_pending = SmartRegionCandidate{};
  m_pending_since_ms = 0;
  return true;
}

void SmartRegionHoverStabilizer::clear() noexcept
{
  m_stable = SmartRegionCandidate{};
  m_pending = SmartRegionCandidate{};
  m_pending_since_ms = 0;
}

bool SmartRegionHoverStabilizer::hasStableCandidate() const noexcept
{
  return m_stable.valid();
}

bool SmartRegionHoverStabilizer::hasPendingCandidate() const noexcept
{
  return m_pending.valid();
}

std::uint64_t SmartRegionHoverStabilizer::pendingSinceMs() const noexcept
{
  return m_pending_since_ms;
}

const SmartRegionCandidate& SmartRegionHoverStabilizer::stableCandidate()
    const noexcept
{
  return m_stable;
}

const SmartRegionCandidate& SmartRegionHoverStabilizer::selectionCandidate()
    const noexcept
{
  return isDetailedCandidate(m_pending) ? m_pending : m_stable;
}

bool SmartRegionHoverRenderGate::update(const SmartRegionCandidate& candidate,
                                        bool has_hover) noexcept
{
  const bool has_changed =
      !m_has_rendered_state || m_has_hover != has_hover ||
      (has_hover && !candidatesEqual(candidate, m_candidate));
  if (has_changed) {
    m_has_rendered_state = true;
    m_has_hover = has_hover;
    m_candidate = has_hover ? candidate : SmartRegionCandidate{};
  }
  return has_changed;
}

bool SmartRegionUpdateGate::shouldProcess(std::uint64_t now_ms) const noexcept
{
  return remainingDelayMs(now_ms) == 0;
}

void SmartRegionUpdateGate::markProcessed(std::uint64_t now_ms) noexcept
{
  markStarted(now_ms);
}

void SmartRegionUpdateGate::markStarted(std::uint64_t now_ms) noexcept
{
  m_has_last_update = true;
  m_last_update_ms = now_ms;
}

std::uint64_t SmartRegionUpdateGate::remainingDelayMs(
    std::uint64_t now_ms) const noexcept
{
  if (!m_has_last_update) {
    return 0;
  }

  const std::uint64_t elapsed_ms = now_ms - m_last_update_ms;
  if (elapsed_ms >= MinimumIntervalMs) {
    return 0;
  }
  return MinimumIntervalMs - elapsed_ms;
}

void SmartRegionUpdateGate::reset() noexcept
{
  m_has_last_update = false;
  m_last_update_ms = 0;
}

void SmartRegionAsyncPresentationGate::recordRawMotion(
    int screen_x, int screen_y, std::uint64_t now_ms) noexcept
{
  m_latest_delta_x = 0;
  m_latest_delta_y = 0;
  m_latest_elapsed_ms = 0;
  if (m_has_raw_motion_sample && now_ms >= m_last_raw_motion_at_ms)
  {
    m_latest_delta_x =
        static_cast<std::int64_t>(screen_x) - m_last_screen_x;
    m_latest_delta_y =
        static_cast<std::int64_t>(screen_y) - m_last_screen_y;
    m_latest_elapsed_ms = now_ms - m_last_raw_motion_at_ms;
    const std::int64_t absolute_delta_x =
        m_latest_delta_x >= 0 ? m_latest_delta_x : -m_latest_delta_x;
    const std::int64_t absolute_delta_y =
        m_latest_delta_y >= 0 ? m_latest_delta_y : -m_latest_delta_y;
    if (m_latest_elapsed_ms <= FastMotionMaximumSampleIntervalMs &&
        (absolute_delta_x >= FastMotionMinimumDeltaPx ||
         absolute_delta_y >= FastMotionMinimumDeltaPx))
    {
      m_last_fast_motion_at_ms = now_ms;
      m_has_fast_motion_sample = true;
    }
  }
  m_last_screen_x = screen_x;
  m_last_screen_y = screen_y;
  m_last_raw_motion_at_ms = now_ms;
  m_has_raw_motion_sample = true;
}

bool SmartRegionAsyncPresentationGate::hasRecentFastMotion(
    std::uint64_t now_ms) const noexcept
{
  return m_has_fast_motion_sample && now_ms >= m_last_fast_motion_at_ms &&
         now_ms - m_last_fast_motion_at_ms < FastMotionHoldMs;
}

bool SmartRegionAsyncPresentationGate::shouldDeferAsyncResult(
    bool is_chromium_browser_chrome, std::uint64_t now_ms) const noexcept
{
  return is_chromium_browser_chrome && hasRecentFastMotion(now_ms);
}

std::int64_t SmartRegionAsyncPresentationGate::latestDeltaX() const noexcept
{
  return m_latest_delta_x;
}

std::int64_t SmartRegionAsyncPresentationGate::latestDeltaY() const noexcept
{
  return m_latest_delta_y;
}

std::uint64_t SmartRegionAsyncPresentationGate::latestElapsedMs() const noexcept
{
  return m_latest_elapsed_ms;
}

void SmartRegionAsyncPresentationGate::reset() noexcept
{
  m_last_screen_x = 0;
  m_last_screen_y = 0;
  m_latest_delta_x = 0;
  m_latest_delta_y = 0;
  m_latest_elapsed_ms = 0;
  m_last_raw_motion_at_ms = 0;
  m_last_fast_motion_at_ms = 0;
  m_has_raw_motion_sample = false;
  m_has_fast_motion_sample = false;
}

bool SmartRegionDetector::detectAt(int screen_x, int screen_y,
                                   SmartRegionCandidate& out,
                                   SmartRegionDiagnosticTrace* diagnostics,
                                   const SmartRegionVisualContext* visual_context,
                                   SmartRegionDetectionPolicy policy,
                                   SmartRegionCandidateCollection* collection,
                                   SmartRegionWindowSnapshot* window_snapshot,
                                   SmartRegionVisualResultCache* visual_cache)
    const noexcept
{
  const std::uint64_t begin_ms = GetTickCount64();
  const bool diagnostic_enabled =
      diagnostics != nullptr && diagnostics->enabled();
  SmartRegionDiagnosticEvent diagnostic_event;
  out = SmartRegionCandidate{};
  if (window_snapshot != nullptr)
  {
    *window_snapshot = SmartRegionWindowSnapshot{};
  }
  WindowDetector window_detector;
  HWND root_window = nullptr;
  WindowRect window_rect;
  const std::uint64_t window_detection_begin_ms =
      diagnostic_enabled ? GetTickCount64() : 0;
  if (!(policy == SmartRegionDetectionPolicy::UiSnapshot
            ? window_detector.snapshotAt(screen_x, screen_y, root_window, window_rect)
            : window_detector.detectAt(screen_x, screen_y, root_window, window_rect))) {
    if (diagnostic_enabled) {
      diagnostic_event.window_detection_attempted = true;
      diagnostic_event.window_detection_ms =
          GetTickCount64() - window_detection_begin_ms;
    }
    recordDetection(diagnostics, out, diagnostic_event, begin_ms);
    return false;
  }
  if (diagnostic_enabled) {
    diagnostic_event.window_detection_attempted = true;
    diagnostic_event.window_detection_ms =
        GetTickCount64() - window_detection_begin_ms;
    if (policy != SmartRegionDetectionPolicy::UiSnapshot)
      recordWindowContext(diagnostic_event, root_window, screen_x, screen_y);
  }

  const POINT screen_point{screen_x, screen_y};
  SmartRegionCandidate candidates[SmartRegionMaxCandidates];
  std::size_t candidate_count = 0;
  WindowRect client_rect;
  const bool has_client_rect =
      getRootClientScreenRect(root_window, client_rect);
  if (window_snapshot != nullptr)
  {
    window_snapshot->root_window =
        reinterpret_cast<std::uintptr_t>(root_window);
    window_snapshot->owner_rect = window_rect;
    window_snapshot->client_rect = client_rect;
  }
  std::size_t uia_candidate_count = 0;
  if (policy == SmartRegionDetectionPolicy::Complete)
  {
    const std::uint64_t uia_lookup_begin_ms =
        diagnostic_enabled ? GetTickCount64() : 0;
    static_cast<void>(window_detail::locateUiaCandidates(
        root_window, screen_point, candidates, SmartRegionMaxUiaCandidates,
        uia_candidate_count));
    candidate_count = uia_candidate_count;
    if (diagnostic_enabled)
    {
      diagnostic_event.uia_lookup_attempted = true;
      diagnostic_event.uia_lookup_ms = GetTickCount64() - uia_lookup_begin_ms;
    }
    const bool has_valid_local_uia_candidate =
        SmartRegionCandidateSelector::hasValidLocalCandidate(
            candidates, uia_candidate_count, screen_x, screen_y, window_rect,
            SmartRegionDiagnosticSource::Uia);
    if (diagnostic_enabled)
    {
      diagnostic_event.uia_has_valid_local_candidate =
          has_valid_local_uia_candidate;
    }

    if (!has_valid_local_uia_candidate &&
        candidate_count < SmartRegionMaxAccessibilityCandidates)
    {
      SmartRegionCandidate msaa_candidate;
      const std::uint64_t msaa_lookup_begin_ms =
          diagnostic_enabled ? GetTickCount64() : 0;
      const bool found_msaa_candidate = window_detail::locateMsaaCandidate(
          root_window, screen_point, msaa_candidate);
      if (diagnostic_enabled)
      {
        diagnostic_event.msaa_lookup_attempted = true;
        diagnostic_event.msaa_candidate_found = found_msaa_candidate;
        diagnostic_event.msaa_lookup_ms =
            GetTickCount64() - msaa_lookup_begin_ms;
      }
      if (found_msaa_candidate)
      {
        candidates[candidate_count++] = msaa_candidate;
      }
    }
  }
  WindowRect chromium_browser_chrome;
  bool use_workbench_visual_policy = false;
  if (policy != SmartRegionDetectionPolicy::WindowOnly &&
      policy != SmartRegionDetectionPolicy::UiSnapshot)
  {
    SmartRegionCandidate known_content;
    const std::uint64_t known_content_lookup_begin_ms =
        diagnostic_enabled ? GetTickCount64() : 0;
    if (window_detail::locateKnownContent(root_window, screen_point,
                                          known_content,
                                          &chromium_browser_chrome,
                                          &use_workbench_visual_policy)) {
      known_content.source = SmartRegionDiagnosticSource::KnownContent;
      known_content.semantic = SmartRegionSemantic::ContentSurface;
      if (candidate_count < SmartRegionMaxCandidates) {
        candidates[candidate_count++] = known_content;
      }
    }
    if (!chromium_browser_chrome.empty() &&
        candidate_count < SmartRegionMaxCandidates)
    {
      candidates[candidate_count++] = makeCandidate(
          root_window, root_window, chromium_browser_chrome,
          SmartRegionKind::KnownContent,
          SmartRegionDiagnosticSource::KnownContent,
          SmartRegionSemantic::ContentSurface);
    }
    if (window_snapshot != nullptr)
    {
      window_snapshot->is_chromium_browser_chrome =
          !chromium_browser_chrome.empty();
    }
    if (diagnostic_enabled) {
      diagnostic_event.known_content_lookup_attempted = true;
      diagnostic_event.known_content_lookup_ms =
          GetTickCount64() - known_content_lookup_begin_ms;
    }
  }

  if (policy != SmartRegionDetectionPolicy::WindowOnly &&
      policy != SmartRegionDetectionPolicy::UiSnapshot &&
      visual_context != nullptr && visual_context->valid() &&
      has_client_rect) {
    window_detail::VisualRegionDiagnostic visual_diagnostic;
    SmartRegionCandidate visual_candidate;
    const std::uint64_t visual_lookup_begin_ms =
        diagnostic_enabled ? GetTickCount64() : 0;
    const WindowRect& visual_owner_rect =
        chromium_browser_chrome.empty() ? client_rect
                                        : chromium_browser_chrome;
    const std::uintptr_t background_identity =
        reinterpret_cast<std::uintptr_t>(
            visual_context->background->pixels.data());
    const std::uint8_t minimum_visual_confidence =
        use_workbench_visual_policy ? kMinimumWorkbenchVisualConfidence
                                    : kMinimumVisualConfidence;
    bool found_visual_region = false;
    const bool allow_browser_wide_fallback_cache =
        !chromium_browser_chrome.empty();
    const bool visual_cache_hit =
        visual_cache != nullptr &&
        visual_cache->lookup(
            reinterpret_cast<std::uintptr_t>(root_window),
            background_identity, visual_owner_rect, screen_x, screen_y,
            visual_candidate, found_visual_region,
            allow_browser_wide_fallback_cache);
    if (!visual_cache_hit)
    {
      found_visual_region = window_detail::findVisualRegionCandidate(
          *visual_context->background, visual_context->image_screen_rect,
          visual_owner_rect, screen_point,
          reinterpret_cast<std::uintptr_t>(root_window), visual_candidate,
          diagnostic_enabled ? &visual_diagnostic : nullptr,
          use_workbench_visual_policy
              ? window_detail::VisualRegionSearchPolicy::ElectronWorkbench
              : window_detail::VisualRegionSearchPolicy::Standard);
      if (visual_cache != nullptr)
      {
        visual_cache->store(
            reinterpret_cast<std::uintptr_t>(root_window),
            background_identity, visual_owner_rect, screen_x, screen_y,
            found_visual_region ? &visual_candidate : nullptr,
            minimum_visual_confidence,
            allow_browser_wide_fallback_cache);
      }
    }
    if (diagnostic_enabled) {
      diagnostic_event.visual_lookup_attempted = !visual_cache_hit;
      diagnostic_event.visual_cache_hit = visual_cache_hit;
      diagnostic_event.visual_lookup_ms = visual_cache_hit
                                              ? 0
                                              : GetTickCount64() -
                                                    visual_lookup_begin_ms;
      diagnostic_event.visual_edge_mask = visual_diagnostic.edge_mask;
      diagnostic_event.visual_cache_contains_candidate =
          visual_cache_hit && found_visual_region;
      diagnostic_event.visual_candidate_confidence =
          found_visual_region ? visual_candidate.visual_confidence : 0;
    }
    if (found_visual_region) {
      if (candidate_count < SmartRegionMaxCandidates) {
        candidates[candidate_count++] = visual_candidate;
      }
    }
  }

  if (has_client_rect && candidate_count < SmartRegionMaxCandidates) {
    candidates[candidate_count++] =
        makeCandidate(root_window, root_window, client_rect,
                      SmartRegionKind::ClientArea,
                      SmartRegionDiagnosticSource::ClientArea,
                      SmartRegionSemantic::Fallback);
  }

  if (candidate_count < SmartRegionMaxCandidates) {
    candidates[candidate_count++] =
        makeCandidate(root_window, root_window, window_rect,
                      SmartRegionKind::Window,
                      SmartRegionDiagnosticSource::Window,
                      SmartRegionSemantic::Fallback);
  }
  const std::uint64_t selection_begin_ms =
      diagnostic_enabled ? GetTickCount64() : 0;
  const std::uint8_t minimum_visual_confidence =
      use_workbench_visual_policy ? kMinimumWorkbenchVisualConfidence
                                  : kMinimumVisualConfidence;
  if (window_snapshot != nullptr)
    window_snapshot->minimum_visual_confidence = minimum_visual_confidence;
  const bool detected =
      diagnostic_enabled
          ? SmartRegionCandidateSelector::selectBest(
                candidates, candidate_count, screen_x, screen_y, window_rect,
                out, diagnostic_event, minimum_visual_confidence)
          : SmartRegionCandidateSelector::selectBest(
                candidates, candidate_count, screen_x, screen_y, window_rect,
                out, minimum_visual_confidence);
  if (diagnostic_enabled) {
    diagnostic_event.selection_ms = GetTickCount64() - selection_begin_ms;
  }
  if (collection != nullptr)
  {
    collection->replace(candidates, candidate_count, screen_x, screen_y,
                        window_rect, out, minimum_visual_confidence);
  }
  recordDetection(diagnostics, out, diagnostic_event, begin_ms);
  return detected;
}

}  // namespace qingying
