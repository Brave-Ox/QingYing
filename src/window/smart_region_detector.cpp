#include "qingying/window/smart_region_detector.hpp"

#include <algorithm>
#include <cstdint>

#include "known_content_locator.hpp"
#include "msaa_region_locator.hpp"
#include "uia_region_locator.hpp"
#include "visual_region_locator.hpp"

namespace qingying {

namespace {

constexpr int kMinimumCandidateWidth = 16;
constexpr int kMinimumCandidateHeight = 16;
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

struct CandidateScoreBreakdown
{
  int source{0};
  int semantic{0};
  int pointer{0};
  int area{0};
  int boundary{0};
  int hierarchy{0};

  int total() const noexcept
  {
    return source + semantic + pointer + area + boundary + hierarchy;
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
    const WindowRect& owner_rect) noexcept
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
  if (candidate.rect.width() < kMinimumCandidateWidth ||
      candidate.rect.height() < kMinimumCandidateHeight) {
    return SmartRegionCandidateRejection::TooSmall;
  }
  if (candidate.source == SmartRegionDiagnosticSource::Visual &&
      candidate.visual_confidence < kMinimumVisualConfidence)
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
                        SmartRegionDiagnosticEvent* diagnostics) noexcept
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
                           owner_rect) !=
        SmartRegionCandidateRejection::None)
    {
      continue;
    }
    for (std::size_t right_index = left_index + 1;
         right_index < processing_count; ++right_index)
    {
      if (duplicate[right_index] ||
          candidateRejection(candidates[right_index], screen_x, screen_y,
                             owner_rect) !=
              SmartRegionCandidateRejection::None ||
          !rectanglesAreNearDuplicates(candidates[left_index].rect,
                                       candidates[right_index].rect))
      {
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

  int best_score = -1;
  std::int64_t best_area = 0;
  std::size_t best_index = processing_count;
  for (std::size_t index = 0; index < processing_count; ++index) {
    const SmartRegionCandidate& candidate = candidates[index];
    SmartRegionCandidateRejection rejection =
        candidateRejection(candidate, screen_x, screen_y, owner_rect);
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

bool SmartRegionVisualContext::valid() const noexcept
{
  return background != nullptr && !background->empty() &&
         !image_screen_rect.empty() &&
         background->width == image_screen_rect.width() &&
         background->height == image_screen_rect.height();
}

bool SmartRegionCandidateSelector::selectBest(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    int screen_x, int screen_y, const WindowRect& owner_rect,
    SmartRegionCandidate& out) noexcept
{
  return selectBestInternal(candidates, candidate_count, screen_x, screen_y,
                            owner_rect, out, nullptr);
}

bool SmartRegionCandidateSelector::selectBest(
    const SmartRegionCandidate* candidates, std::size_t candidate_count,
    int screen_x, int screen_y, const WindowRect& owner_rect,
    SmartRegionCandidate& out, SmartRegionDiagnosticEvent& diagnostics) noexcept
{
  return selectBestInternal(candidates, candidate_count, screen_x, screen_y,
                            owner_rect, out, &diagnostics);
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
    std::uint64_t elapsed_ms) noexcept
{
  if (!m_enabled || !m_has_latest_event) {
    return false;
  }
  m_latest_event.overlay_render_ms = elapsed_ms;
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

  if (candidatesEqual(candidate, m_stable)) {
    m_pending = SmartRegionCandidate{};
    m_pending_since_ms = 0;
    return false;
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

bool SmartRegionDetector::detectAt(int screen_x, int screen_y,
                                   SmartRegionCandidate& out,
                                   SmartRegionDiagnosticTrace* diagnostics,
                                   const SmartRegionVisualContext* visual_context)
    const noexcept
{
  const std::uint64_t begin_ms = GetTickCount64();
  const bool diagnostic_enabled =
      diagnostics != nullptr && diagnostics->enabled();
  SmartRegionDiagnosticEvent diagnostic_event;
  out = SmartRegionCandidate{};
  WindowDetector window_detector;
  HWND root_window = nullptr;
  WindowRect window_rect;
  const std::uint64_t window_detection_begin_ms =
      diagnostic_enabled ? GetTickCount64() : 0;
  if (!window_detector.detectAt(screen_x, screen_y, root_window, window_rect)) {
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
  }

  const POINT screen_point{screen_x, screen_y};
  SmartRegionCandidate candidates[SmartRegionMaxCandidates];
  std::size_t candidate_count = 0;
  WindowRect client_rect;
  const bool has_client_rect =
      getRootClientScreenRect(root_window, client_rect);
  const std::uint64_t uia_lookup_begin_ms =
      diagnostic_enabled ? GetTickCount64() : 0;
  std::size_t uia_candidate_count = 0;
  static_cast<void>(window_detail::locateUiaCandidates(
      root_window, screen_point, candidates, SmartRegionMaxCandidates,
      uia_candidate_count));
  candidate_count = uia_candidate_count;
  if (diagnostic_enabled) {
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
      candidate_count < SmartRegionMaxCandidates)
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
  SmartRegionCandidate known_content;
  const std::uint64_t known_content_lookup_begin_ms =
      diagnostic_enabled ? GetTickCount64() : 0;
  if (window_detail::locateKnownContent(root_window, screen_point,
                                        known_content)) {
    known_content.source = SmartRegionDiagnosticSource::KnownContent;
    known_content.semantic = SmartRegionSemantic::ContentSurface;
    if (candidate_count < SmartRegionMaxCandidates) {
      candidates[candidate_count++] = known_content;
    }
  }
  if (diagnostic_enabled) {
    diagnostic_event.known_content_lookup_attempted = true;
    diagnostic_event.known_content_lookup_ms =
        GetTickCount64() - known_content_lookup_begin_ms;
  }

  if (visual_context != nullptr && visual_context->valid() &&
      has_client_rect) {
    window_detail::VisualRegionDiagnostic visual_diagnostic;
    SmartRegionCandidate visual_candidate;
    const std::uint64_t visual_lookup_begin_ms =
        diagnostic_enabled ? GetTickCount64() : 0;
    const bool found_visual_region = window_detail::findVisualRegionCandidate(
        *visual_context->background, visual_context->image_screen_rect,
        client_rect, screen_point,
        reinterpret_cast<std::uintptr_t>(root_window), visual_candidate,
        diagnostic_enabled ? &visual_diagnostic : nullptr);
    if (diagnostic_enabled) {
      diagnostic_event.visual_lookup_attempted = true;
      diagnostic_event.visual_lookup_ms =
          GetTickCount64() - visual_lookup_begin_ms;
      diagnostic_event.visual_edge_mask = visual_diagnostic.edge_mask;
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
  const bool detected =
      diagnostic_enabled
          ? SmartRegionCandidateSelector::selectBest(
                candidates, candidate_count, screen_x, screen_y, window_rect,
                out, diagnostic_event)
          : SmartRegionCandidateSelector::selectBest(
                candidates, candidate_count, screen_x, screen_y, window_rect,
                out);
  if (diagnostic_enabled) {
    diagnostic_event.selection_ms = GetTickCount64() - selection_begin_ms;
  }
  recordDetection(diagnostics, out, diagnostic_event, begin_ms);
  return detected;
}

}  // namespace qingying
