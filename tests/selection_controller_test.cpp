#include "qingying/overlay/selection_controller.hpp"

#include <gtest/gtest.h>

namespace qingying {

TEST(SelectionControllerTest, InitialStateIsCancelledEmpty) {
  SelectionController controller;
  const SelectionResult& sel = controller.selection();
  EXPECT_TRUE(sel.cancelled);
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
  const SelectionResult& sel = controller.selection();
  EXPECT_FALSE(sel.cancelled);
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
  const SelectionResult& sel = controller.selection();
  EXPECT_FALSE(sel.cancelled);
  EXPECT_EQ(sel.x, 100);
  EXPECT_EQ(sel.y, 50);
  EXPECT_EQ(sel.width, 200);
  EXPECT_EQ(sel.height, 200);
}

TEST(SelectionControllerTest, PartialAxisUpdateKeepsBothAxes) {
  SelectionController controller;
  controller.begin(0, 0);
  controller.update(0, 100);  // 只在一个轴上拖动
  controller.confirm();
  const SelectionResult& sel = controller.selection();
  EXPECT_FALSE(sel.cancelled);
  EXPECT_EQ(sel.width, 0);
  EXPECT_EQ(sel.height, 100);
}

TEST(SelectionControllerTest, ClickWithoutDragIsCancelled) {
  SelectionController controller;
  controller.begin(10, 20);
  controller.confirm();  // 未拖动即松开
  const SelectionResult& sel = controller.selection();
  EXPECT_TRUE(sel.cancelled);
}

TEST(SelectionControllerTest, CancelMarksSelectionCancelled) {
  SelectionController controller;
  controller.begin(10, 20);
  controller.update(200, 300);
  controller.cancel();
  const SelectionResult& sel = controller.selection();
  EXPECT_TRUE(sel.cancelled);
}

TEST(SelectionControllerTest, NewBeginResetsPreviousSelection) {
  SelectionController controller;
  controller.begin(10, 20);
  controller.update(200, 300);
  controller.confirm();
  EXPECT_FALSE(controller.selection().cancelled);

  controller.begin(0, 0);  // 开始新一次框选，旧结果应被清除
  EXPECT_TRUE(controller.selection().cancelled);
  EXPECT_EQ(controller.selection().width, 0);
  EXPECT_EQ(controller.selection().height, 0);
}

TEST(SelectionControllerTest, LiveSelectionUpdatesDuringDrag) {
  SelectionController controller;
  controller.begin(10, 10);
  controller.update(50, 60);
  // 拖动中未 confirm，选区也应实时可见（UI 壳据此绘制选框）
  EXPECT_FALSE(controller.selection().cancelled);
  EXPECT_EQ(controller.selection().x, 10);
  EXPECT_EQ(controller.selection().y, 10);
  EXPECT_EQ(controller.selection().width, 40);
  EXPECT_EQ(controller.selection().height, 50);
}

}  // namespace qingying
