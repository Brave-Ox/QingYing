#include "browser_shell_presentation.hpp"

namespace qingying {
namespace {

constexpr std::uint64_t AdjacentCandidateDelayMs = 8;

bool contains(const WindowRect& rect, POINT point) noexcept
{
  return !rect.empty() && point.x >= rect.left && point.x < rect.right &&
         point.y >= rect.top && point.y < rect.bottom;
}

bool sameCandidate(const BrowserPresentedCandidate& left,
                   const BrowserPresentedCandidate& right) noexcept
{
  return left.m_identity_hash == right.m_identity_hash &&
         left.m_window_generation == right.m_window_generation &&
         left.m_layout_generation == right.m_layout_generation;
}

int quality(BrowserPresentationLevel level) noexcept
{
  return static_cast<int>(level);
}

}  // namespace

BrowserPresentationDecision BrowserShellPresentationState::update(
    const BrowserPresentedCandidate& incoming, POINT screen_point,
    bool fast_motion, std::uint64_t now_ms) noexcept
{
  if (incoming.m_hit_rect.empty())
  {
    clear();
    return {BrowserPresentationAction::Clear, 0,
            BrowserPresentationReason::CurrentCandidateLeft};
  }
  if (!m_has_current)
  {
    m_current = incoming;
    m_has_current = true;
    return {BrowserPresentationAction::Apply, 0,
            BrowserPresentationReason::FirstCandidate};
  }
  if (incoming.m_window_generation < m_current.m_window_generation)
  {
    return {BrowserPresentationAction::Keep, 0,
            BrowserPresentationReason::WindowGenerationExpired};
  }
  if (incoming.m_layout_generation < m_current.m_layout_generation)
  {
    return {BrowserPresentationAction::Keep, 0,
            BrowserPresentationReason::LayoutGenerationExpired};
  }
  if (incoming.m_pointer_sequence < m_current.m_pointer_sequence)
  {
    return {BrowserPresentationAction::Keep, 0,
            BrowserPresentationReason::PointerSequenceExpired};
  }
  if (sameCandidate(m_current, incoming))
  {
    m_has_pending = false;
    if (quality(incoming.m_level) > quality(m_current.m_level))
    {
      m_current = incoming;
      return {BrowserPresentationAction::Apply, 0,
              BrowserPresentationReason::QualityUpgrade};
    }
    m_current = incoming;
    return {};
  }
  if (contains(m_current.m_hit_rect, screen_point) &&
      quality(incoming.m_level) < quality(m_current.m_level))
  {
    return {BrowserPresentationAction::Keep, 0,
            BrowserPresentationReason::LowQualityFallback};
  }
  if (!contains(m_current.m_hit_rect, screen_point))
  {
    m_current = incoming;
    m_has_pending = false;
    return {BrowserPresentationAction::Apply, 0,
            BrowserPresentationReason::CurrentCandidateLeft};
  }
  if (fast_motion)
  {
    return {BrowserPresentationAction::Defer, AdjacentCandidateDelayMs,
            BrowserPresentationReason::FastMotion};
  }
  if (!m_has_pending || !sameCandidate(m_pending, incoming))
  {
    m_pending = incoming;
    m_pending_since_ms = now_ms;
    m_has_pending = true;
    return {BrowserPresentationAction::Defer, AdjacentCandidateDelayMs,
            BrowserPresentationReason::AdjacentCandidateWait};
  }
  m_current = incoming;
  m_has_pending = false;
  return {BrowserPresentationAction::Apply, 0,
          BrowserPresentationReason::AdjacentCandidateWait};
}

void BrowserShellPresentationState::clear() noexcept
{
  m_current = {};
  m_pending = {};
  m_pending_since_ms = 0;
  m_has_current = false;
  m_has_pending = false;
}

bool BrowserShellPresentationState::hasCurrent() const noexcept
{
  return m_has_current;
}

const BrowserPresentedCandidate& BrowserShellPresentationState::current()
    const noexcept
{
  return m_current;
}

}  // namespace qingying
