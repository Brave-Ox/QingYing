#include "qingying/overlay/overlay_phase.hpp"

namespace qingying {

bool overlayPhaseHasSelection(OverlayPhase phase) noexcept {
  switch (phase) {
    case OverlayPhase::Selected:
    case OverlayPhase::LongShotRunning:
    case OverlayPhase::LongShotPaused:
    case OverlayPhase::LongShotFinishing:
      return true;
    case OverlayPhase::Sniffing:
    case OverlayPhase::Creating:
    case OverlayPhase::Closing:
      return false;
  }
  return false;
}

bool overlayPhaseIsLongShot(OverlayPhase phase) noexcept {
  return phase == OverlayPhase::LongShotRunning ||
         phase == OverlayPhase::LongShotPaused ||
         phase == OverlayPhase::LongShotFinishing;
}

bool canTransitionOverlayPhase(OverlayPhase from, OverlayPhase to) noexcept {
  if (from == to) {
    return true;
  }
  if (to == OverlayPhase::Closing) {
    return from != OverlayPhase::Closing;
  }

  switch (from) {
    case OverlayPhase::Sniffing:
      return to == OverlayPhase::Creating || to == OverlayPhase::Selected;
    case OverlayPhase::Creating:
      return to == OverlayPhase::Sniffing || to == OverlayPhase::Selected;
    case OverlayPhase::Selected:
      return to == OverlayPhase::Creating ||
             to == OverlayPhase::LongShotRunning;
    case OverlayPhase::LongShotRunning:
      return to == OverlayPhase::LongShotPaused ||
             to == OverlayPhase::LongShotFinishing ||
             to == OverlayPhase::Selected;
    case OverlayPhase::LongShotPaused:
      return to == OverlayPhase::LongShotRunning ||
             to == OverlayPhase::LongShotFinishing ||
             to == OverlayPhase::Selected;
    case OverlayPhase::LongShotFinishing:
      return to == OverlayPhase::Selected;
    case OverlayPhase::Closing:
      return false;
  }
  return false;
}

bool transitionOverlayPhase(OverlayPhase& current, OverlayPhase next) noexcept {
  if (!canTransitionOverlayPhase(current, next)) {
    return false;
  }
  current = next;
  return true;
}

}  // namespace qingying
