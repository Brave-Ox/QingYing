#include "qingying/app/capture_session.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>

TEST(CaptureSessionTest, EmptyByDefault) {
  qingying::CaptureSession session;
  EXPECT_FALSE(session.hasResult());
  EXPECT_TRUE(session.result().empty());
}

TEST(CaptureSessionTest, SetResultThenReadBack) {
  qingying::CaptureSession session;

  qingying::Image image;
  image.width = 4;
  image.height = 3;
  image.pixels.assign(12, 0xFF0000FFu);  // 12 opaque red pixels (BGRA)

  session.setResult(std::move(image));

  EXPECT_TRUE(session.hasResult());
  EXPECT_EQ(session.result().width, 4);
  EXPECT_EQ(session.result().height, 3);
  EXPECT_EQ(session.result().pixels.size(), 12u);
}

TEST(CaptureSessionTest, ClearDropsResult) {
  qingying::CaptureSession session;

  qingying::Image image;
  image.width = 1;
  image.height = 1;
  image.pixels.assign(1, 0u);
  session.setResult(std::move(image));

  session.clear();
  EXPECT_FALSE(session.hasResult());
  EXPECT_TRUE(session.result().empty());
}

TEST(CaptureSessionTest, CompatibilityFacadeOnlyMutatesGuiScope) {
  qingying::ResultStore store;
  const auto external = store.publish(2, qingying::Image{1, 1, {99}});
  qingying::CaptureSession session(store);
  session.setResult(qingying::Image{1, 1, {11}});
  EXPECT_EQ(session.result().pixels[0], 11u);
  session.clear();
  EXPECT_FALSE(session.hasResult());
  EXPECT_TRUE(store.acquire(2, external));
}
