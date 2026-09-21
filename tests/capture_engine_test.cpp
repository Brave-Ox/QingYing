#include "qingying/capture/capture_engine.hpp"

#include <Windows.h>  // GetSystemMetrics / SM_CXSCREEN — PIMPL 头文件不带 Windows 依赖

#include <gtest/gtest.h>

#include <limits>
#include <string>

namespace qingying {

TEST(CaptureEngineTest, CaptureFullScreenProducesNonEmptyImage) {
  CaptureEngine engine;
  Image out;

  const int screen_w = GetSystemMetrics(SM_CXSCREEN);
  const int screen_h = GetSystemMetrics(SM_CYSCREEN);
  ASSERT_GT(screen_w, 0);
  ASSERT_GT(screen_h, 0);

  const ActionResult result =
      engine.captureRegion(0, 0, screen_w, screen_h, out);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kOk);
  EXPECT_EQ(out.width, screen_w);
  EXPECT_EQ(out.height, screen_h);
  EXPECT_FALSE(out.empty());
  EXPECT_EQ(out.pixels.size(), static_cast<std::size_t>(screen_w) * screen_h);
}

TEST(CaptureEngineTest, CaptureRegionHasExpectedDimensions) {
  CaptureEngine engine;
  Image out;

  const ActionResult result = engine.captureRegion(10, 20, 320, 200, out);

  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kOk);
  EXPECT_EQ(out.width, 320);
  EXPECT_EQ(out.height, 200);
  EXPECT_FALSE(out.empty());
}

TEST(CaptureEngineTest, CaptureRegionPixelsAreBgra) {
  CaptureEngine engine;
  Image out;

  const ActionResult result = engine.captureRegion(0, 0, 1, 1, out);

  ASSERT_TRUE(result.ok);
  ASSERT_EQ(out.width, 1);
  ASSERT_EQ(out.height, 1);

  // BGRA32 layout: alpha (top byte) is opaque after a GDI capture.
  const std::uint32_t pixel = out.pixels[0];
  EXPECT_EQ((pixel >> 24) & 0xFFu, 0xFFu);
}

TEST(CaptureEngineTest, CaptureInvalidRegionReturnsError) {
  CaptureEngine engine;
  Image out;

  const ActionResult result = engine.captureRegion(-5, -5, 0, 0, out);

  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error_code, ErrorCode::kOk);
  EXPECT_TRUE(out.empty());
}

TEST(CaptureEngineTest, CaptureRegionRejectsCoordinateAndStrideOverflow) {
  CaptureEngine engine;
  Image out{1, 1, {1}};
  EXPECT_EQ(engine.captureRegion((std::numeric_limits<int>::max)(), 0, 1, 1,
                                 out).error_code,
            ErrorCode::kInvalidArgument);
  EXPECT_TRUE(out.empty());
  EXPECT_EQ(engine.captureRegion(0, 0,
                                 (std::numeric_limits<int>::max)(), 1, out)
                .error_code,
            ErrorCode::kInvalidArgument);
  EXPECT_TRUE(out.empty());
}

TEST(CaptureEngineTest, FailedDesktopCaptureIncludesWin32Diagnostic) {
  CaptureEngine engine;
  Image out;

  const ActionResult result = engine.captureRegion(0, 0, 1, 1, out);

  if (!result.ok) {
    EXPECT_NE(result.message.find("win32="), std::string::npos);
  }
}

}  // namespace qingying
