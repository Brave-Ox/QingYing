#include <gtest/gtest.h>

#include "browser_shell_presentation.hpp"

namespace qingying {
namespace {

BrowserPresentedCandidate makeCandidate(
    BrowserPresentationLevel level, WindowRect rect, std::uint64_t identity,
    std::uint64_t pointer_sequence = 1)
{
  BrowserPresentedCandidate candidate;
  candidate.m_level = level;
  candidate.m_hit_rect = rect;
  candidate.m_identity_hash = identity;
  candidate.m_window_generation = 3;
  candidate.m_layout_generation = 5;
  candidate.m_pointer_sequence = pointer_sequence;
  return candidate;
}

TEST(BrowserShellPresentationStateTest, AppliesFirstAndQualityUpgradesImmediately)
{
  BrowserShellPresentationState state;
  EXPECT_EQ(state.update(makeCandidate(BrowserPresentationLevel::Coarse,
                                       {0, 0, 100, 40}, 1),
                         {20, 20}, false, 10).m_action,
            BrowserPresentationAction::Apply);
  EXPECT_EQ(state.update(makeCandidate(BrowserPresentationLevel::Cached,
                                       {0, 0, 100, 40}, 1),
                         {20, 20}, false, 11).m_action,
            BrowserPresentationAction::Apply);
  EXPECT_EQ(state.update(makeCandidate(BrowserPresentationLevel::Exact,
                                       {0, 0, 100, 40}, 1),
                         {20, 20}, false, 12).m_action,
            BrowserPresentationAction::Apply);
}

TEST(BrowserShellPresentationState, KeepsExactWhenLateCoarseStillContainsPointer)
{
  BrowserShellPresentationState state;
  ASSERT_EQ(state.update(makeCandidate(BrowserPresentationLevel::Exact,
                                       {0, 0, 100, 40}, 1),
                         {20, 20}, false, 10).m_action,
            BrowserPresentationAction::Apply);
  const BrowserPresentationDecision decision = state.update(
      makeCandidate(BrowserPresentationLevel::Coarse, {0, 0, 200, 60}, 2),
      {20, 20}, false, 11);
  EXPECT_EQ(decision.m_action, BrowserPresentationAction::Keep);
  EXPECT_EQ(decision.m_reason, BrowserPresentationReason::LowQualityFallback);
}

TEST(BrowserShellPresentationState, DefersAdjacentExactUntilSecondSample)
{
  BrowserShellPresentationState state;
  ASSERT_EQ(state.update(makeCandidate(BrowserPresentationLevel::Exact,
                                       {0, 0, 50, 40}, 1),
                         {35, 20}, false, 10).m_action,
            BrowserPresentationAction::Apply);
  EXPECT_EQ(state.update(makeCandidate(BrowserPresentationLevel::Exact,
                                       {30, 0, 80, 40}, 2),
                         {35, 20}, false, 11).m_action,
            BrowserPresentationAction::Defer);
  EXPECT_EQ(state.update(makeCandidate(BrowserPresentationLevel::Exact,
                                       {30, 0, 80, 40}, 2),
                         {35, 20}, false, 12).m_action,
            BrowserPresentationAction::Apply);
}

TEST(BrowserShellPresentationState, RejectsExpiredPresentationTokens)
{
  BrowserShellPresentationState state;
  ASSERT_EQ(state.update(makeCandidate(BrowserPresentationLevel::Exact,
                                       {0, 0, 100, 40}, 1, 8),
                         {20, 20}, false, 10).m_action,
            BrowserPresentationAction::Apply);

  BrowserPresentedCandidate old_window =
      makeCandidate(BrowserPresentationLevel::Exact, {0, 0, 100, 40}, 2, 9);
  --old_window.m_window_generation;
  EXPECT_EQ(state.update(old_window, {20, 20}, false, 11).m_reason,
            BrowserPresentationReason::WindowGenerationExpired);

  BrowserPresentedCandidate old_layout =
      makeCandidate(BrowserPresentationLevel::Exact, {0, 0, 100, 40}, 2, 9);
  --old_layout.m_layout_generation;
  EXPECT_EQ(state.update(old_layout, {20, 20}, false, 12).m_reason,
            BrowserPresentationReason::LayoutGenerationExpired);

  EXPECT_EQ(state.update(makeCandidate(BrowserPresentationLevel::Exact,
                                       {0, 0, 100, 40}, 2, 7),
                         {20, 20}, false, 13).m_reason,
            BrowserPresentationReason::PointerSequenceExpired);
}

TEST(BrowserShellPresentationState,
     KeepsVisibleCandidateForTwoPixelJitterButRefreshesHitRect)
{
  BrowserShellPresentationState state;
  ASSERT_EQ(state.update(makeCandidate(BrowserPresentationLevel::Exact,
                                       {10, 10, 50, 40}, 1),
                         {20, 20}, false, 10).m_action,
            BrowserPresentationAction::Apply);
  const BrowserPresentationDecision decision = state.update(
      makeCandidate(BrowserPresentationLevel::Exact, {12, 8, 52, 42}, 1),
      {20, 20}, false, 11);

  EXPECT_EQ(decision.m_action, BrowserPresentationAction::Keep);
  ASSERT_TRUE(state.hasCurrent());
  EXPECT_EQ(state.current().m_hit_rect.left, 12);
  EXPECT_EQ(state.current().m_hit_rect.top, 8);
  EXPECT_EQ(state.current().m_hit_rect.right, 52);
  EXPECT_EQ(state.current().m_hit_rect.bottom, 42);
}

}  // namespace
}  // namespace qingying
