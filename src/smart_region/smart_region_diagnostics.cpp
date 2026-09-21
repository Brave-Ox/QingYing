#include "qingying/window/smart_region_diagnostics.hpp"

#include <algorithm>

namespace qingying {

void SmartRegionDiagnosticTrace::setEnabled(bool enabled) noexcept {
  m_enabled = enabled;
  if (!m_enabled) {
    m_has_latest_event = false;
    m_latest_event = SmartRegionDiagnosticEvent{};
  }
}

bool SmartRegionDiagnosticTrace::enabled() const noexcept {
  return m_enabled;
}

bool SmartRegionDiagnosticTrace::record(
    const SmartRegionDiagnosticEvent& event) noexcept {
  if (!m_enabled) return false;
  m_latest_event = event;
  m_has_latest_event = true;
  return true;
}

bool SmartRegionDiagnosticTrace::recordOverlayRenderElapsed(
    std::uint64_t elapsed_ms, std::uint64_t render_count) noexcept {
  if (!m_enabled || !m_has_latest_event) return false;
  m_latest_event.overlay_render_ms = elapsed_ms;
  m_latest_event.overlay_render_count = render_count;
  return true;
}

bool SmartRegionDiagnosticTrace::recordOverlayRenderCount(
    std::uint64_t render_count) noexcept {
  if (!m_enabled || !m_has_latest_event) return false;
  m_latest_event.overlay_render_count = render_count;
  return true;
}

bool SmartRegionDiagnosticTrace::recordHoverInputDelay(
    std::uint64_t delay_ms) noexcept {
  if (!m_enabled || !m_has_latest_event) return false;
  m_latest_event.hover_input_delay_ms = delay_ms;
  return true;
}

bool SmartRegionDiagnosticTrace::recordStabilizationDelay(
    std::uint64_t delay_ms) noexcept {
  if (!m_enabled || !m_has_latest_event) return false;
  m_latest_event.stabilization_delay_ms = delay_ms;
  return true;
}

bool SmartRegionDiagnosticTrace::recordHoverMotion(
    std::int64_t delta_x, std::int64_t delta_y, std::uint64_t elapsed_ms,
    bool fast) noexcept {
  if (!m_enabled || !m_has_latest_event) return false;
  m_latest_event.hover_motion_delta_x = delta_x;
  m_latest_event.hover_motion_delta_y = delta_y;
  m_latest_event.hover_motion_elapsed_ms = elapsed_ms;
  m_latest_event.hover_motion_fast = fast;
  return true;
}

bool SmartRegionDiagnosticTrace::recordBrowserShellAtlas(
    bool hit, bool miss, BrowserShellAtlasInvalidReason invalid_reason,
    std::uint64_t hit_test_ms, std::uint64_t first_frame_ms) noexcept {
  if (!m_enabled || !m_has_latest_event) return false;
  m_latest_event.browser_shell_atlas_hit = hit;
  m_latest_event.browser_shell_atlas_miss = miss;
  m_latest_event.browser_shell_atlas_invalid_reason = invalid_reason;
  m_latest_event.browser_shell_atlas_hit_test_ms = hit_test_ms;
  m_latest_event.browser_shell_first_frame_ms = first_frame_ms;
  return true;
}

bool SmartRegionDiagnosticTrace::recordAsyncUiaResult(
    std::uint64_t request_id, std::uint64_t elapsed_ms,
    std::uint64_t age_ms, std::uint64_t poll_delay_ms, bool succeeded,
    bool msaa_attempted, bool browser_semantic_miss, bool cache_hit,
    bool suppressed_by_cooldown, std::size_t candidate_count,
    bool matches_current_request, bool applied, bool deferred,
    SmartRegionAsyncDeferralReason deferral_reason,
    const SmartRegionMsaaTraversalDiagnostic& msaa_diagnostic,
    const SmartRegionCandidate* candidates,
    std::size_t diagnostic_candidate_count) noexcept {
  if (!m_enabled || !m_has_latest_event) return false;
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
  m_latest_event.uia_async_matches_current_request = matches_current_request;
  m_latest_event.uia_async_result_applied = applied;
  m_latest_event.uia_async_result_deferred = deferred;
  m_latest_event.uia_async_deferral_reason = deferral_reason;
  m_latest_event.uia_async_msaa_diagnostic = msaa_diagnostic;
  m_latest_event.uia_async_diagnostic_candidate_count = 0;
  for (std::size_t index = 0;
       index < SmartRegionDiagnosticMaxCandidates; ++index) {
    m_latest_event.uia_async_candidates[index] = SmartRegionCandidate{};
  }
  if (candidates != nullptr) {
    const std::size_t copied_count =
        (std::min)(diagnostic_candidate_count, SmartRegionDiagnosticMaxCandidates);
    for (std::size_t index = 0; index < copied_count; ++index) {
      m_latest_event.uia_async_candidates[index] = candidates[index];
    }
    m_latest_event.uia_async_diagnostic_candidate_count = copied_count;
  }
  return true;
}

bool SmartRegionDiagnosticTrace::hasLatestEvent() const noexcept {
  return m_has_latest_event;
}

const SmartRegionDiagnosticEvent& SmartRegionDiagnosticTrace::latestEvent()
    const noexcept {
  return m_latest_event;
}

}  // namespace qingying
