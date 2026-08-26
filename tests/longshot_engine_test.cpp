#include "qingying/capture/capture_engine.hpp"
#include "qingying/longshot/longshot_engine.hpp"

#include <gtest/gtest.h>

namespace qingying {

TEST(LongShotEngineTest, InvalidSelectionRequestClearsOutput) {
  CaptureEngine capture;
  LongShotEngine engine(capture);
  LongShotRequest request;
  Image out;
  out.width = 1;
  out.height = 1;
  out.pixels.push_back(0xFFFFFFFFu);

  const ActionResult result = engine.captureSelection(request, out);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kInvalidArgument);
  EXPECT_TRUE(out.empty());
}

TEST(LongShotEngineTest, InvalidWindowIsUnsupported) {
  CaptureEngine capture;
  LongShotEngine engine(capture);
  LongShotRequest request;
  request.owner_window = 1;
  request.x = 100;
  request.y = 200;
  request.width = 640;
  request.height = 480;
  Image out;

  const ActionResult result = engine.captureSelection(request, out);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.error_code, ErrorCode::kLongShotUnsupported);
  EXPECT_TRUE(out.empty());
}

}  // namespace qingying
