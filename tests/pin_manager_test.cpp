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
