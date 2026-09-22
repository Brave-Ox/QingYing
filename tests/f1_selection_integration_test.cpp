#include "qingying/capture/capture_engine.hpp"
#include "qingying/overlay/coordinate_transform.hpp"
#include "qingying/overlay/selection_controller.hpp"
#include "qingying/overlay/selection_handles.hpp"

#include "desktop_test_environment.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace qingying {

// 端到端验证 F1 选区链路：状态机（创建 → 八点调整 → 整体移动）
// → 物理像素出参 → 真实 CaptureEngine 区域截图。
// 不依赖具体屏幕内容，只验证坐标与尺寸契约。
TEST(F1SelectionIntegrationTest, SelectionToPhysicalPixelsToCapture) {
  const auto desktop = test::probeDesktopCapture();
  if (!desktop.available) GTEST_SKIP() << desktop.diagnostic;

  const coord::VirtualScreenRect screen = coord::getVirtualScreen();
  ASSERT_GT(screen.width, 0);
  ASSERT_GT(screen.height, 0);

  SelectionController controller;
  controller.setBounds(screen.width, screen.height);

  // 1. 拖出矩形：从 (10,10) 到 (210,160)，得 200x150。
  controller.begin(10, 10);
  controller.update(210, 160);
  controller.confirm();
  ASSERT_FALSE(controller.selection().empty());
  EXPECT_EQ(controller.selection().x, 10);
  EXPECT_EQ(controller.selection().y, 10);
  EXPECT_EQ(controller.selection().width, 200);
  EXPECT_EQ(controller.selection().height, 150);

  // 2. 八点命中：右下角命中 BottomRight 手柄，并从该手柄调整大小。
  const SelectionHandle handle = controller.hitTest(209, 159);
  EXPECT_EQ(handle, SelectionHandle::BottomRight);
  controller.beginResize(handle, 209, 159);
  controller.updateResize(259, 209);  // 向右下各扩 50
  controller.endDrag();
  EXPECT_EQ(controller.selection().width, 250);
  EXPECT_EQ(controller.selection().height, 200);

  // 3. 整体移动：平移 (+30,+20)。
  controller.beginMove(100, 100);
  controller.updateMove(130, 120);
  controller.endDrag();
  EXPECT_EQ(controller.selection().x, 40);
  EXPECT_EQ(controller.selection().y, 30);

  // 4. 出参转物理（屏幕）坐标。
  const OverlayClientRect& client_selection = controller.selection();
  const ScreenPhysicalRect sel =
      coord::clientToScreen(client_selection, screen);

  // 5. 用真实 CaptureEngine 按物理像素截图，验证宽高与像素数量契约。
  CaptureEngine engine;
  Image image;
  const ActionResult result = engine.captureRegion(sel, image);
  ASSERT_TRUE(result.ok) << result.message;
  EXPECT_EQ(image.width, sel.width);
  EXPECT_EQ(image.height, sel.height);
  EXPECT_FALSE(image.empty());
  EXPECT_EQ(image.pixels.size(),
            static_cast<std::size_t>(sel.width) * sel.height);
}

}  // namespace qingying
