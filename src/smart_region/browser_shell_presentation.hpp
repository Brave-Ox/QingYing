#pragma once

#include <cstdint>

#include "browser_shell_types.hpp"

namespace qingying {

class BrowserShellPresentationState
{
 public:
  BrowserPresentationDecision update(const BrowserPresentedCandidate& incoming,
                                     POINT screen_point, bool fast_motion,
                                     std::uint64_t now_ms) noexcept;
  void clear() noexcept;
  bool hasCurrent() const noexcept;
  const BrowserPresentedCandidate& current() const noexcept;

 private:
  BrowserPresentedCandidate m_current;
  BrowserPresentedCandidate m_pending;
  std::uint64_t m_pending_since_ms{0};
  bool m_has_current{false};
  bool m_has_pending{false};
};

}  // namespace qingying
