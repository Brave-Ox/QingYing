#include "qingying/overlay/overlay_phase.hpp"

#include <gtest/gtest.h>

namespace qingying {

TEST(OverlayPhaseTest, SelectionAndLongShotPredicatesAreExplicit) {
  EXPECT_FALSE(overlayPhaseHasSelection(OverlayPhase::Sniffing));
  EXPECT_FALSE(overlayPhaseHasSelection(OverlayPhase::Creating));
  EXPECT_TRUE(overlayPhaseHasSelection(OverlayPhase::Selected));
  EXPECT_TRUE(overlayPhaseHasSelection(OverlayPhase::LongShotRunning));
  EXPECT_TRUE(overlayPhaseHasSelection(OverlayPhase::LongShotPaused));
  EXPECT_TRUE(overlayPhaseHasSelection(OverlayPhase::LongShotFinishing));
  EXPECT_FALSE(overlayPhaseHasSelection(OverlayPhase::Closing));

  EXPECT_FALSE(overlayPhaseIsLongShot(OverlayPhase::Selected));
  EXPECT_TRUE(overlayPhaseIsLongShot(OverlayPhase::LongShotRunning));
  EXPECT_TRUE(overlayPhaseIsLongShot(OverlayPhase::LongShotPaused));
  EXPECT_TRUE(overlayPhaseIsLongShot(OverlayPhase::LongShotFinishing));
}

TEST(OverlayPhaseTest, AcceptsNormalSelectionAndLongShotLifecycle) {
  OverlayPhase phase = OverlayPhase::Sniffing;
  EXPECT_TRUE(transitionOverlayPhase(phase, OverlayPhase::Creating));
  EXPECT_TRUE(transitionOverlayPhase(phase, OverlayPhase::Selected));
  EXPECT_TRUE(transitionOverlayPhase(phase, OverlayPhase::LongShotRunning));
  EXPECT_TRUE(transitionOverlayPhase(phase, OverlayPhase::LongShotPaused));
  EXPECT_TRUE(transitionOverlayPhase(phase, OverlayPhase::LongShotRunning));
  EXPECT_TRUE(transitionOverlayPhase(phase, OverlayPhase::LongShotFinishing));
  EXPECT_TRUE(transitionOverlayPhase(phase, OverlayPhase::Selected));
  EXPECT_TRUE(transitionOverlayPhase(phase, OverlayPhase::Closing));
}

TEST(OverlayPhaseTest, RejectsIllegalOrPostCloseTransitions) {
  OverlayPhase phase = OverlayPhase::Sniffing;
  EXPECT_FALSE(transitionOverlayPhase(phase, OverlayPhase::LongShotRunning));
  EXPECT_EQ(phase, OverlayPhase::Sniffing);

  EXPECT_TRUE(transitionOverlayPhase(phase, OverlayPhase::Closing));
  EXPECT_FALSE(transitionOverlayPhase(phase, OverlayPhase::Selected));
  EXPECT_EQ(phase, OverlayPhase::Closing);
}

}  // namespace qingying
