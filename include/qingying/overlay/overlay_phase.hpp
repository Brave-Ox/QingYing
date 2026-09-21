#pragma once

namespace qingying {

// SelectionOverlay 的顶层交互阶段。拖拽类型属于 Selected / Creating
// 内部的短生命周期输入状态，不再与长截图生命周期共用 bool 组合。
enum class OverlayPhase {
  Sniffing,
  Creating,
  Selected,
  LongShotRunning,
  LongShotPaused,
  LongShotFinishing,
  LongShotRecoverable,
  LongShotResultPending,
  Closing,
};

bool overlayPhaseHasSelection(OverlayPhase phase) noexcept;
bool overlayPhaseIsLongShot(OverlayPhase phase) noexcept;
bool overlayPhaseIsLongShotCaptureActive(OverlayPhase phase) noexcept;
bool canTransitionOverlayPhase(OverlayPhase from, OverlayPhase to) noexcept;
bool transitionOverlayPhase(OverlayPhase& current, OverlayPhase next) noexcept;

}  // namespace qingying
