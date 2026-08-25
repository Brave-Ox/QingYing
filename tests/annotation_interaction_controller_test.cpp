#include "qingying/annotate/annotation_interaction_controller.hpp"

#include <gtest/gtest.h>

namespace qingying {
namespace {

constexpr int kCanvasWidth = 100;
constexpr int kCanvasHeight = 80;

void prepare(AnnotationInteractionController& controller)
{
  controller.setCanvasSize(kCanvasWidth, kCanvasHeight);
  controller.setTool(AnnotationTool::Rectangle);
}

}  // namespace

TEST(AnnotationInteractionControllerTest, DefaultToolIsRectangle)
{
  AnnotationInteractionController controller;
  controller.setCanvasSize(kCanvasWidth, kCanvasHeight);
  EXPECT_EQ(controller.tool(), AnnotationTool::Rectangle);
}

TEST(AnnotationInteractionControllerTest, SetToolSwitchesCurrentTool)
{
  AnnotationInteractionController controller;
  prepare(controller);

  controller.setTool(AnnotationTool::Arrow);
  EXPECT_EQ(controller.tool(), AnnotationTool::Arrow);

  controller.setTool(AnnotationTool::Pen);
  EXPECT_EQ(controller.tool(), AnnotationTool::Pen);
}

TEST(AnnotationInteractionControllerTest, BeginStrokeOutsideCanvasIsRejected)
{
  AnnotationInteractionController controller;
  prepare(controller);

  EXPECT_FALSE(controller.beginStroke(-1.0f, 10.0f));
  EXPECT_FALSE(controller.beginStroke(10.0f, 80.0f));  // y == height：边界外
  EXPECT_FALSE(controller.isDrawing());
}

TEST(AnnotationInteractionControllerTest, BeginStrokeWithNoneToolIsRejected)
{
  AnnotationInteractionController controller;
  prepare(controller);
  controller.setTool(AnnotationTool::None);

  EXPECT_FALSE(controller.beginStroke(10.0f, 10.0f));
}

TEST(AnnotationInteractionControllerTest, RectangleStrokeAddsNormalizedBoundsOnEnd)
{
  AnnotationInteractionController controller;
  prepare(controller);
  AnnotationEngine engine;

  ASSERT_TRUE(controller.beginStroke(30.0f, 40.0f));
  EXPECT_TRUE(controller.isDrawing());
  EXPECT_EQ(engine.document().count(), 0u);  // 拖拽中不入栈

  controller.updateStroke(10.0f, 20.0f);  // 反向拖
  EXPECT_TRUE(controller.hasPreview());
  EXPECT_EQ(controller.preview().type, AnnotationType::Rectangle);
  EXPECT_FLOAT_EQ(controller.preview().bounds.x, 10.0f);
  EXPECT_FLOAT_EQ(controller.preview().bounds.y, 20.0f);
  EXPECT_FLOAT_EQ(controller.preview().bounds.width, 20.0f);
  EXPECT_FLOAT_EQ(controller.preview().bounds.height, 20.0f);

  ASSERT_TRUE(controller.endStroke(engine));
  EXPECT_FALSE(controller.isDrawing());
  EXPECT_FALSE(controller.hasPreview());
  ASSERT_EQ(engine.document().count(), 1u);
  EXPECT_FLOAT_EQ(engine.document().items().at(0).bounds.x, 10.0f);
  EXPECT_FLOAT_EQ(engine.document().items().at(0).bounds.y, 20.0f);
}

TEST(AnnotationInteractionControllerTest, TinyRectangleIsRejectedOnEnd)
{
  AnnotationInteractionController controller;
  prepare(controller);
  AnnotationEngine engine;

  ASSERT_TRUE(controller.beginStroke(10.0f, 10.0f));
  controller.updateStroke(10.5f, 10.5f);  // 小于 MinAnnotationSizePx

  EXPECT_FALSE(controller.endStroke(engine));
  EXPECT_TRUE(engine.document().empty());
  EXPECT_FALSE(controller.hasPreview());
}

TEST(AnnotationInteractionControllerTest, ArrowStrokeUsesStartAndEnd)
{
  AnnotationInteractionController controller;
  prepare(controller);
  controller.setTool(AnnotationTool::Arrow);
  AnnotationEngine engine;

  ASSERT_TRUE(controller.beginStroke(5.0f, 5.0f));
  controller.updateStroke(50.0f, 40.0f);
  ASSERT_TRUE(controller.endStroke(engine));

  ASSERT_EQ(engine.document().count(), 1u);
  const Annotation& arrow = engine.document().items().at(0);
  EXPECT_EQ(arrow.type, AnnotationType::Arrow);
  EXPECT_FLOAT_EQ(arrow.start.x, 5.0f);
  EXPECT_FLOAT_EQ(arrow.start.y, 5.0f);
  EXPECT_FLOAT_EQ(arrow.end.x, 50.0f);
  EXPECT_FLOAT_EQ(arrow.end.y, 40.0f);
}

TEST(AnnotationInteractionControllerTest, PenStrokeCollectsPointsAndDoesNotClose)
{
  AnnotationInteractionController controller;
  prepare(controller);
  controller.setTool(AnnotationTool::Pen);
  AnnotationEngine engine;

  ASSERT_TRUE(controller.beginStroke(1.0f, 1.0f));
  controller.updateStroke(2.0f, 3.0f);
  controller.updateStroke(4.0f, 5.0f);
  ASSERT_TRUE(controller.endStroke(engine));

  ASSERT_EQ(engine.document().count(), 1u);
  const Annotation& pen = engine.document().items().at(0);
  EXPECT_EQ(pen.type, AnnotationType::Pen);
  ASSERT_EQ(pen.points.size(), 3u);
  EXPECT_FLOAT_EQ(pen.points.at(0).x, 1.0f);
  EXPECT_FLOAT_EQ(pen.points.at(2).y, 5.0f);
}

TEST(AnnotationInteractionControllerTest, SinglePointPenIsRejectedOnEnd)
{
  AnnotationInteractionController controller;
  prepare(controller);
  controller.setTool(AnnotationTool::Pen);
  AnnotationEngine engine;

  ASSERT_TRUE(controller.beginStroke(1.0f, 1.0f));
  // 未移动：只有起点
  EXPECT_FALSE(controller.endStroke(engine));
  EXPECT_TRUE(engine.document().empty());
}

TEST(AnnotationInteractionControllerTest, CancelStrokeDropsPreviewWithoutAdding)
{
  AnnotationInteractionController controller;
  prepare(controller);
  AnnotationEngine engine;

  ASSERT_TRUE(controller.beginStroke(10.0f, 10.0f));
  controller.updateStroke(40.0f, 40.0f);
  controller.cancelStroke();

  EXPECT_FALSE(controller.isDrawing());
  EXPECT_FALSE(controller.hasPreview());
  EXPECT_TRUE(engine.document().empty());
}

TEST(AnnotationInteractionControllerTest, UndoDelegatesToEngine)
{
  AnnotationInteractionController controller;
  prepare(controller);
  AnnotationEngine engine;

  ASSERT_TRUE(controller.beginStroke(10.0f, 10.0f));
  controller.updateStroke(40.0f, 40.0f);
  ASSERT_TRUE(controller.endStroke(engine));
  EXPECT_EQ(engine.document().count(), 1u);

  ASSERT_TRUE(controller.undo(engine));
  EXPECT_TRUE(engine.document().empty());
  EXPECT_TRUE(engine.canRedo());
}

TEST(AnnotationInteractionControllerTest, UndoWhileDrawingCancelsPreviewFirst)
{
  AnnotationInteractionController controller;
  prepare(controller);
  AnnotationEngine engine;

  ASSERT_TRUE(controller.beginStroke(10.0f, 10.0f));
  controller.updateStroke(40.0f, 40.0f);
  ASSERT_TRUE(controller.endStroke(engine));

  ASSERT_TRUE(controller.beginStroke(20.0f, 20.0f));
  controller.updateStroke(60.0f, 60.0f);
  EXPECT_TRUE(controller.hasPreview());

  // 拖拽中撤销：先清预览，再撤销上一笔
  ASSERT_TRUE(controller.undo(engine));
  EXPECT_FALSE(controller.isDrawing());
  EXPECT_FALSE(controller.hasPreview());
  EXPECT_TRUE(engine.document().empty());
}

TEST(AnnotationInteractionControllerTest, DeferredToolsCannotBeginStroke)
{
  AnnotationInteractionController controller;
  prepare(controller);

  // Task 7：椭圆 / 文字 / 马赛克本轮仅留位，交互层不得起笔。
  controller.setTool(AnnotationTool::Ellipse);
  EXPECT_FALSE(controller.beginStroke(10.0f, 10.0f));

  controller.setTool(AnnotationTool::Text);
  EXPECT_FALSE(controller.beginStroke(10.0f, 10.0f));

  controller.setTool(AnnotationTool::Mosaic);
  EXPECT_FALSE(controller.beginStroke(10.0f, 10.0f));

  EXPECT_FALSE(controller.isDrawing());
}

}  // namespace qingying
