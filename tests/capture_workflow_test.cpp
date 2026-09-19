#include "qingying/app/capture_workflow.hpp"

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/capture_service.h"
#include "qingying/app/longshot_controller.hpp"
#include "qingying/app/longshot_limits_provider.hpp"
#include "qingying/app/result_action_service.h"
#include "qingying/capture/capture_engine.hpp"
#include "qingying/export/export_service.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <gtest/gtest.h>

namespace qingying {
namespace {

SelectionIntent selectionFor(SelectionAction action) {
  SelectionIntent selection;
  selection.cancelled = false;
  selection.x = 10;
  selection.y = 20;
  selection.width = 300;
  selection.height = 200;
  selection.action = action;
  return selection;
}

LongShotOutcome longShotOutcome(LongShotStopReason reason, int frames) {
  LongShotOutcome outcome;
  outcome.stop_reason = reason;
  outcome.accepted_frames = frames;
  if (frames > 0) {
    outcome.image = Image{1, frames, std::vector<std::uint32_t>(
                                         static_cast<std::size_t>(frames),
                                         0xFF123456u)};
  }
  return outcome;
}

}  // namespace

TEST(CaptureWorkflowRouteTest, CancelledOrEmptySelectionDoesNothing) {
  SelectionResult cancelled = selectionFor(SelectionAction::Copy);
  cancelled.cancelled = true;
  EXPECT_EQ(decideCaptureWorkflowRoute(cancelled, true),
            CaptureWorkflowRoute::None);

  const SelectionResult empty = selectionFor(SelectionAction::None);
  EXPECT_EQ(decideCaptureWorkflowRoute(empty, false),
            CaptureWorkflowRoute::None);
}

TEST(CaptureWorkflowRouteTest, EditIntentHasHighestPriority) {
  const SelectionIntent selection = selectionFor(SelectionAction::Edit);

  EXPECT_EQ(decideCaptureWorkflowRoute(selection, true, false),
            CaptureWorkflowRoute::Edit);
}

TEST(CaptureWorkflowRouteTest, AnnotatedResultUsesExplicitReadyState) {
  const SelectionIntent selection = selectionFor(SelectionAction::Save);

  EXPECT_EQ(decideCaptureWorkflowRoute(selection, false, true),
            CaptureWorkflowRoute::AnnotatedResult);
}

TEST(CaptureWorkflowRouteTest, LongShotIntentStartsLongShot) {
  const SelectionResult selection =
      selectionFor(SelectionAction::LongShot);

  EXPECT_EQ(decideCaptureWorkflowRoute(selection, false),
            CaptureWorkflowRoute::StartLongShot);
}

TEST(CaptureWorkflowRouteTest, CompletedLongShotFeedsResultAction) {
  const SelectionResult selection = selectionFor(SelectionAction::Save);

  EXPECT_EQ(decideCaptureWorkflowRoute(selection, true),
            CaptureWorkflowRoute::ExistingLongShotResult);
}

TEST(CaptureWorkflowRouteTest, OrdinaryResultActionsCaptureRegion) {
  EXPECT_EQ(decideCaptureWorkflowRoute(
                selectionFor(SelectionAction::Copy), false),
            CaptureWorkflowRoute::CaptureRegion);
  EXPECT_EQ(decideCaptureWorkflowRoute(
                selectionFor(SelectionAction::Save), false),
            CaptureWorkflowRoute::CaptureRegion);
  EXPECT_EQ(decideCaptureWorkflowRoute(
                selectionFor(SelectionAction::Pin), false),
            CaptureWorkflowRoute::CaptureRegion);
}

TEST(CaptureWorkflowLongShotDecisionTest, CompleteResultPublishesAndCopies) {
  const LongShotOutcome outcome =
      longShotOutcome(LongShotStopReason::ReachedBottom, 2);

  const LongShotWorkflowDecision decision = decideLongShotWorkflow(outcome);

  EXPECT_EQ(decision.disposition,
            LongShotWorkflowDisposition::PublishAndCopy);
}

TEST(CaptureWorkflowLongShotDecisionTest, CancellationAlwaysDiscards) {
  const LongShotOutcome outcome =
      longShotOutcome(LongShotStopReason::Cancelled, 2);

  const LongShotWorkflowDecision decision = decideLongShotWorkflow(outcome);

  EXPECT_EQ(decision.disposition, LongShotWorkflowDisposition::Discard);
}

TEST(CaptureWorkflowLongShotDecisionTest, UnsupportedKeepsSelectionWithoutResult) {
  LongShotOutcome outcome;
  outcome.stop_reason = LongShotStopReason::RequestRejected;
  outcome.diagnostic.error_code = ErrorCode::kLongShotUnsupported;

  const LongShotWorkflowDecision decision = decideLongShotWorkflow(outcome);

  EXPECT_EQ(decision.disposition,
            LongShotWorkflowDisposition::AwaitConfirmation);
  EXPECT_EQ(decision.recovery.result, LongShotRecoveryResult::None);
  EXPECT_EQ(decision.recovery.cause, LongShotRecoveryCause::Unsupported);
}

TEST(CaptureWorkflowLongShotDecisionTest, SingleFrameStaysOrdinaryCapture) {
  const LongShotOutcome outcome =
      longShotOutcome(LongShotStopReason::CaptureFailed, 1);

  const LongShotWorkflowDecision decision = decideLongShotWorkflow(outcome);

  EXPECT_EQ(decision.disposition,
            LongShotWorkflowDisposition::AwaitConfirmation);
  EXPECT_EQ(decision.recovery.result,
            LongShotRecoveryResult::SingleFrame);
  EXPECT_EQ(decision.recovery.cause, LongShotRecoveryCause::CaptureFailed);
}

TEST(CaptureWorkflowLongShotDecisionTest, CompositeFailureAwaitsExplicitAcceptance) {
  const LongShotOutcome outcome =
      longShotOutcome(LongShotStopReason::MatchFailed, 3);

  const LongShotWorkflowDecision decision = decideLongShotWorkflow(outcome);

  EXPECT_EQ(decision.disposition,
            LongShotWorkflowDisposition::AwaitConfirmation);
  EXPECT_EQ(decision.recovery.result,
            LongShotRecoveryResult::PartialResult);
  EXPECT_EQ(decision.recovery.cause, LongShotRecoveryCause::MatchFailed);
}

}  // namespace qingying


TEST(CaptureWorkflowLifetimeTest, CancelAndShutdownPreserveExternalResults) {
  using namespace qingying;
  ActionDispatcher dispatcher;
  CaptureEngine capture;
  LongShotEngine engine(capture);
  SelectionOverlay overlay;
  LongShotLimitsProvider limits_provider;
  LongShotController controller(engine, overlay, limits_provider);
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  InteractionGate gate;
  CaptureService capture_service(capture, store, pins, gate);
  ResultActionService actions(store, exporter, pins);
  CaptureWorkflow workflow(capture, capture_service, controller, store,
                           actions, pins, overlay, &gate);
  const auto external = store.publish(2, Image{1, 1, {22}});
  store.publish(Image{1, 1, {11}});
  workflow.cancel();
  EXPECT_EQ(store.currentId(), kInvalidResultId);
  EXPECT_TRUE(store.acquire(2, external));
  store.publish(Image{1, 1, {33}});
  workflow.shutdown();
  EXPECT_EQ(store.currentId(), kInvalidResultId);
  EXPECT_TRUE(store.acquire(2, external));
}
