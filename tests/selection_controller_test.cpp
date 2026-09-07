#include "qingying/overlay/selection_controller.hpp"

#include <gtest/gtest.h>

namespace qingying {

TEST(SelectionControllerTest, InitialStateIsCancelledEmpty) {
  SelectionController controller;
  const OverlayClientRect& sel = controller.selection();
  EXPECT_TRUE(sel.empty());
  EXPECT_EQ(sel.x, 0);
  EXPECT_EQ(sel.y, 0);
  EXPECT_EQ(sel.width, 0);
  EXPECT_EQ(sel.height, 0);
}

TEST(SelectionControllerTest, DragProducesNormalizedRect) {
  SelectionController controller;
  controller.begin(100, 50);
  controller.update(300, 250);
  controller.confirm();
  const OverlayClientRect& sel = controller.selection();
  EXPECT_FALSE(sel.empty());
  EXPECT_EQ(sel.x, 100);
  EXPECT_EQ(sel.y, 50);
  EXPECT_EQ(sel.width, 200);
  EXPECT_EQ(sel.height, 200);
}

TEST(SelectionControllerTest, ReverseDragIsNormalized) {
  SelectionController controller;
  controller.begin(300, 250);
  controller.update(100, 50);
  controller.confirm();
  const OverlayClientRect& sel = controller.selection();
  EXPECT_FALSE(sel.empty());
  EXPECT_EQ(sel.x, 100);
  EXPECT_EQ(sel.y, 50);
  EXPECT_EQ(sel.width, 200);
  EXPECT_EQ(sel.height, 200);
}

TEST(SelectionControllerTest, PartialAxisUpdateKeepsBothAxes) {
  SelectionController controller;
  controller.begin(0, 0);
  controller.update(0, 100);  // 只在一个轴上拖动
  // 拖动中两个轴都保留（实时选区），宽高恒非负。
  EXPECT_TRUE(controller.selection().empty());
  EXPECT_EQ(controller.selection().width, 0);
  EXPECT_EQ(controller.selection().height, 100);

  controller.confirm();
  // 3.3 契约：width 必须 > 0 才能确认 → 0 宽视为取消。
  EXPECT_TRUE(controller.selection().empty());
}

TEST(SelectionControllerTest, ClickWithoutDragIsCancelled) {
  SelectionController controller;
  controller.begin(10, 20);
  controller.confirm();  // 未拖动即松开
  const OverlayClientRect& sel = controller.selection();
  EXPECT_TRUE(sel.empty());
}

TEST(SelectionControllerTest, ReportsMovementOnlyAfterPointerLeavesStart)
{
  SelectionController controller;
  controller.begin(10, 20);

  EXPECT_FALSE(controller.hasMovedFromStart(10, 20));
  EXPECT_TRUE(controller.hasMovedFromStart(11, 20));
  EXPECT_TRUE(controller.hasMovedFromStart(10, 21));
  controller.confirm();
  EXPECT_FALSE(controller.hasMovedFromStart(11, 20));
}

TEST(SelectionControllerTest, CancelMarksSelectionCancelled) {
  SelectionController controller;
  controller.begin(10, 20);
  controller.update(200, 300);
  controller.cancel();
  const OverlayClientRect& sel = controller.selection();
  EXPECT_TRUE(sel.empty());
}

TEST(SelectionControllerTest, NewBeginResetsPreviousSelection) {
  SelectionController controller;
  controller.begin(10, 20);
  controller.update(200, 300);
  controller.confirm();
  EXPECT_FALSE(controller.selection().empty());

  controller.begin(0, 0);  // 开始新一次框选，旧结果应被清除
  EXPECT_TRUE(controller.selection().empty());
  EXPECT_EQ(controller.selection().width, 0);
  EXPECT_EQ(controller.selection().height, 0);
}

TEST(SelectionControllerTest, LiveSelectionUpdatesDuringDrag) {
  SelectionController controller;
  controller.begin(10, 10);
  controller.update(50, 60);
  // 拖动中未 confirm，选区也应实时可见（UI 壳据此绘制选框）
  EXPECT_FALSE(controller.selection().empty());
  EXPECT_EQ(controller.selection().x, 10);
  EXPECT_EQ(controller.selection().y, 10);
  EXPECT_EQ(controller.selection().width, 40);
  EXPECT_EQ(controller.selection().height, 50);
}

TEST(SelectionControllerTest, ConfirmRequiresPositiveWidthAndHeight) {
  SelectionController controller;
  controller.begin(10, 20);
  controller.update(50, 20);  // 宽度 50、高度 0
  controller.confirm();
  EXPECT_TRUE(controller.selection().empty());
}

TEST(SelectionControllerTest, MoveClampsToBounds) {
  SelectionController c;
  c.setBounds(300, 200);
  c.begin(10, 10);
  c.update(110, 60);  // 100x50 at (10,10)
  c.confirm();
  ASSERT_FALSE(c.selection().empty());

  c.beginMove(50, 30);
  c.updateMove(400, 300);  // 大幅拖动，应钳制到边界内
  c.endDrag();
  const OverlayClientRect& s = c.selection();
  EXPECT_EQ(s.x, 300 - s.width);
  EXPECT_EQ(s.y, 200 - s.height);
}

TEST(SelectionControllerTest, ResizeRightHandleExpandsKeepingLeft) {
  SelectionController c;
  c.setBounds(300, 200);
  c.begin(10, 10);
  c.update(60, 60);  // 50x50 at (10,10)
  c.confirm();

  c.beginResize(SelectionHandle::Right, 60, 35);
  c.updateResize(110, 35);  // dx +50
  c.endDrag();
  const OverlayClientRect& s = c.selection();
  EXPECT_EQ(s.x, 10);       // 左边固定
  EXPECT_EQ(s.y, 10);
  EXPECT_EQ(s.width, 100);  // 50 + 50
  EXPECT_EQ(s.height, 50);  // 高度不变
}

TEST(SelectionControllerTest, ResizeLeftHandleKeepsRightEdge) {
  SelectionController c;
  c.setBounds(300, 200);
  c.begin(60, 10);
  c.update(110, 60);  // 50x50 at (60,10)
  c.confirm();

  c.beginResize(SelectionHandle::Left, 60, 35);
  c.updateResize(10, 35);  // dx -50
  c.endDrag();
  const OverlayClientRect& s = c.selection();
  EXPECT_EQ(s.width, 100);         // 左移 50，宽度变大
  EXPECT_EQ(s.x, 10);              // 新左边界
  EXPECT_EQ(s.x + s.width, 110);   // 右边界固定 = 60 + 50
}

TEST(SelectionControllerTest, ResizeRespectsMinSize) {
  SelectionController c;
  c.setBounds(300, 200);
  c.begin(10, 10);
  c.update(110, 110);  // 100x100 at (10,10)
  c.confirm();

  c.beginResize(SelectionHandle::Left, 10, 60);
  c.updateResize(300, 60);  // 向左越过右边界，触发最小尺寸
  c.endDrag();
  const OverlayClientRect& s = c.selection();
  EXPECT_GE(s.width, 1);   // 最小尺寸 1
  EXPECT_EQ(s.x, 109);     // 右边界 110 固定，宽 1 → x = 109
}

TEST(SelectionControllerTest, ResizeClampsToBounds) {
  SelectionController c;
  c.setBounds(300, 200);
  c.begin(10, 10);
  c.update(60, 60);  // 50x50 at (10,10)
  c.confirm();

  c.beginResize(SelectionHandle::Right, 60, 35);
  c.updateResize(500, 35);  // 向右拖出 300 边界
  c.endDrag();
  const OverlayClientRect& s = c.selection();
  EXPECT_EQ(s.x, 10);
  EXPECT_EQ(s.x + s.width, 300);  // 右边界贴住 bounds
}

TEST(SelectionControllerTest, HitTestAfterConfirm) {
  SelectionController c;
  c.setBounds(400, 300);
  c.begin(100, 80);
  c.update(300, 230);  // 200x150 at (100,80)
  c.confirm();
  ASSERT_FALSE(c.selection().empty());

  EXPECT_EQ(c.hitTest(200, 155), SelectionHandle::Move);   // 内部
  EXPECT_EQ(c.hitTest(100, 80), SelectionHandle::TopLeft); // 角
  EXPECT_EQ(c.hitTest(50, 50), SelectionHandle::None);     // 外部
}

TEST(SelectionControllerTest, HitTestWithoutSelectionReturnsNone) {
  SelectionController c;
  EXPECT_EQ(c.hitTest(10, 10), SelectionHandle::None);
}

}  // namespace qingying
