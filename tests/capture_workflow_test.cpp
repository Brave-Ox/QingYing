#include "qingying/app/capture_workflow.hpp"

#include <gtest/gtest.h>

namespace qingying {
namespace {

SelectionResult selectionFor(SelectionAction action) {
  SelectionResult selection;
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
  SelectionResult selection = selectionFor(SelectionAction::Edit);
  selection.annotated_image = Image{1, 1, {0xFFFFFFFFu}};

  EXPECT_EQ(decideCaptureWorkflowRoute(selection, true),
            CaptureWorkflowRoute::Edit);
}

TEST(CaptureWorkflowRouteTest, AnnotatedImageUsesExistingResult) {
  SelectionResult selection = selectionFor(SelectionAction::LongShot);
  selection.annotated_image = Image{1, 1, {0xFFFFFFFFu}};

  EXPECT_EQ(decideCaptureWorkflowRoute(selection, false),
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
