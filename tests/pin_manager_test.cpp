#include "qingying/action/image.hpp"
#include "qingying/pin/pin_manager.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

qingying::Image makeImage(int width, int height) {
  qingying::Image image;
  image.width = width;
  image.height = height;
  image.pixels.assign(static_cast<std::size_t>(width) *
                          static_cast<std::size_t>(height),
                      0xFF3366CCu);
  return image;
}

}  // namespace

TEST(PinManagerTest, EmptyByDefault) {
  qingying::PinManager manager;

  EXPECT_EQ(manager.count(), 0);
}

TEST(PinWindowTest, ChromeScaleUsesWindowDpiAndFallsBackTo96) {
  EXPECT_EQ(qingying::PinWindow::scaleForDpi(28, 96), 28);
  EXPECT_EQ(qingying::PinWindow::scaleForDpi(28, 144), 42);
  EXPECT_EQ(qingying::PinWindow::scaleForDpi(28, 0), 28);
}

TEST(PinManagerTest, RejectsEmptyOrMalformedImage) {
  qingying::PinManager manager;

  EXPECT_FALSE(manager.show(qingying::Image{}));

  qingying::Image malformed;
  malformed.width = 2;
  malformed.height = 2;
  malformed.pixels.assign(3, 0u);
  EXPECT_FALSE(manager.show(malformed));
  EXPECT_EQ(manager.count(), 0);
}

TEST(PinManagerTest, SupportsMultipleWindowsAndCloseAll) {
  qingying::PinManager manager;
  const qingying::Image image = makeImage(320, 180);

  ASSERT_TRUE(manager.show(image));
  ASSERT_TRUE(manager.show(image));
  EXPECT_EQ(manager.count(), 2);

  manager.closeAll();
  EXPECT_EQ(manager.count(), 0);
}

TEST(PinManagerTest, DestructorClosesWindows) {
  {
    qingying::PinManager manager;
    ASSERT_TRUE(manager.show(makeImage(160, 120)));
    EXPECT_EQ(manager.count(), 1);
  }

  // A second manager can create a window after the first manager has cleaned
  // up its native windows and callbacks.
  qingying::PinManager manager;
  ASSERT_TRUE(manager.show(makeImage(160, 120)));
  EXPECT_EQ(manager.count(), 1);
  manager.closeAll();
}

TEST(PinManagerTest, NewPinsAreLaidOutWithoutOverlap) {
  qingying::PinManager manager;
  const qingying::Image image = makeImage(320, 180);

  // 连续添加多枚钉图：位置靠右依次排列，任意两枚不得重叠。
  ASSERT_TRUE(manager.show(image));
  ASSERT_TRUE(manager.show(image));
  ASSERT_TRUE(manager.show(image));
  ASSERT_TRUE(manager.show(image));
  ASSERT_EQ(manager.count(), 4);

  const std::vector<RECT> rects = manager.windowRects();
  ASSERT_EQ(rects.size(), 4u);

  const auto overlaps = [](const RECT& a, const RECT& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom &&
           a.bottom > b.top;
  };
  for (std::size_t i = 0; i < rects.size(); ++i) {
    EXPECT_FALSE(rects[i].right <= rects[i].left ||
                 rects[i].bottom <= rects[i].top)  // 有效矩形
        << "pin " << i << " has invalid rect";
    for (std::size_t j = i + 1; j < rects.size(); ++j) {
      EXPECT_FALSE(overlaps(rects[i], rects[j]))
          << "pin " << i << " and pin " << j << " overlap";
    }
  }

  manager.closeAll();
}

TEST(PinManagerTest, AgentPinsHaveStableIdsAndSeparateGlobalUsage) {
  using namespace qingying;
  PinManager manager({}, [](PinWindow&, int, int) { return true; });
  const auto gui = manager.showGui(makeImage(1, 1));
  const auto first = manager.showAgent(makeImage(2, 1), [] { return true; });
  const auto second = manager.showAgent(makeImage(1, 2), [] { return true; });
  ASSERT_TRUE(gui);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(gui.pin_id, first.pin_id);
  EXPECT_NE(first.pin_id, second.pin_id);
  EXPECT_EQ(manager.source(gui.pin_id), PinSource::Gui);
  EXPECT_EQ(manager.source(first.pin_id), PinSource::Agent);
  EXPECT_EQ(manager.agentUsage().count, 2u);
  EXPECT_EQ(manager.agentUsage().bytes, 16u);
  manager.closeAll();
  EXPECT_EQ(manager.agentUsage().count, 0u);
  EXPECT_EQ(manager.agentUsage().bytes, 0u);
}

TEST(PinManagerTest, NinthAgentPinIsRejectedBeforeWindowCreation) {
  using namespace qingying;
  int presentations = 0;
  PinManager manager({}, [&](PinWindow&, int, int) {
    ++presentations;
    return true;
  });
  for (int i = 0; i < 8; ++i)
    ASSERT_TRUE(manager.showAgent(makeImage(1, 1), [] { return true; }));
  const auto ninth = manager.showAgent(makeImage(1, 1), [] { return true; });
  EXPECT_EQ(ninth.error_code, ErrorCode::kResourceLimit);
  EXPECT_EQ(presentations, 8);
  EXPECT_EQ(manager.agentUsage().count, 8u);
}

TEST(PinManagerTest, AgentByteLimitAndFailedCreationRefundReservation) {
  using namespace qingying;
  AutomationLimits limits;
  limits.max_agent_pin_bytes = 8;
  int presentations = 0;
  PinManager manager(limits, [&](PinWindow&, int, int) {
    ++presentations;
    return presentations != 2;
  });
  ASSERT_TRUE(manager.showAgent(makeImage(1, 1), [] { return true; }));
  const auto failed = manager.showAgent(makeImage(1, 1), [] { return true; });
  EXPECT_EQ(failed.error_code, ErrorCode::kUnknown);
  EXPECT_EQ(manager.agentUsage().count, 1u);
  EXPECT_EQ(manager.agentUsage().bytes, 4u);
  const auto oversized = manager.showAgent(makeImage(2, 1), [] { return true; });
  EXPECT_EQ(oversized.error_code, ErrorCode::kResourceLimit);
  EXPECT_EQ(presentations, 2);
}

TEST(PinManagerTest, CancellationAndRepeatedCloseRefundAtMostOnce) {
  using namespace qingying;
  int presentations = 0;
  PinManager manager({}, [&](PinWindow&, int, int) {
    ++presentations;
    return true;
  });
  const auto cancelled = manager.showAgent(makeImage(1, 1), [] { return false; });
  EXPECT_EQ(cancelled.error_code, ErrorCode::kCancelled);
  EXPECT_EQ(presentations, 0);
  EXPECT_EQ(manager.agentUsage().count, 0u);
  ASSERT_TRUE(manager.showAgent(makeImage(1, 1), [] { return true; }));
  manager.closeAll();
  manager.closeAll();
  EXPECT_EQ(manager.agentUsage().count, 0u);
  EXPECT_EQ(manager.agentUsage().bytes, 0u);
}
