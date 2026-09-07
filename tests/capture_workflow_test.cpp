#include "qingying/app/capture_workflow.hpp"

#include "qingying/action/action_dispatcher.hpp"
#include "qingying/app/longshot_controller.hpp"
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

}  // namespace qingying


TEST(CaptureWorkflowLifetimeTest, CancelAndShutdownPreserveExternalResults) {
  using namespace qingying;
  ActionDispatcher dispatcher;
  CaptureEngine capture;
  LongShotEngine engine(capture);
  SelectionOverlay overlay;
  LongShotController controller(engine, overlay);
  ResultStore store;
  ExportService exporter;
  PinManager pins;
  ResultActionService actions(store, exporter, pins);
  CaptureWorkflow workflow(dispatcher, capture, controller, store, actions, pins, overlay);
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
