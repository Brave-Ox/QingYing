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
constexpr int kSourceScoreUia = 4000;
constexpr int kSourceScoreMsaa = 3750;
constexpr int kSourceScoreVisual = 3500;
constexpr int kSourceScoreKnownContent = 3000;
constexpr int kSourceScoreClientArea = 1000;
constexpr int kSourceScoreWindow = 500;
// 鼠标下可独立操作的控件优先于其所在的泛化内容容器。
constexpr int kSemanticScoreActionable = 900;
constexpr int kSemanticScoreContent = 800;
constexpr int kMaximumAreaSpecificityScore = 600;
constexpr std::int64_t kAreaSpecificityDivisor = 2000;

bool candidatesEqual(const SmartRegionCandidate& left,
                     const SmartRegionCandidate& right) noexcept
{
  return left.owner_window == right.owner_window &&
         left.target_window == right.target_window &&
         left.rect.left == right.rect.left && left.rect.top == right.rect.top &&
         left.rect.right == right.rect.right &&
         left.rect.bottom == right.rect.bottom && left.kind == right.kind;
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

int candidateScore(const SmartRegionCandidate& candidate) noexcept
{
  const std::int64_t area = areaOf(candidate.rect);
  const std::int64_t area_penalty = area / kAreaSpecificityDivisor;
  int area_score = static_cast<int>((std::max)(
      std::int64_t{0}, kMaximumAreaSpecificityScore - area_penalty));
  if (candidate.source == SmartRegionDiagnosticSource::Visual)
  {
    area_score /= 2;
  }
  return sourceScore(diagnosticSourceFor(candidate)) +
         semanticScore(candidate.semantic) + area_score;
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

  const std::size_t diagnostic_count =
      (std::min)(candidate_count, SmartRegionDiagnosticMaxCandidates);
  if (diagnostics != nullptr) {
    diagnostics->candidate_count = diagnostic_count;
  }

  int best_score = -1;
  std::int64_t best_area = 0;
  std::size_t best_index = candidate_count;
  for (std::size_t index = 0; index < candidate_count; ++index) {
    const SmartRegionCandidate& candidate = candidates[index];
    const SmartRegionCandidateRejection rejection =
        candidateRejection(candidate, screen_x, screen_y, owner_rect);
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

    const int score = candidateScore(candidate);
    if (diagnostics != nullptr && index < diagnostic_count) {
      diagnostics->candidates[index].score = score;
    }
    const std::int64_t area = areaOf(candidate.rect);
    if (score > best_score ||
        (score == best_score && (best_area == 0 || area < best_area)) ||
        (score == best_score && area == best_area &&
         candidate.target_window < out.target_window)) {
      out = candidate;
      best_score = score;
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
