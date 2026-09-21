#include "qingying/command/command_parser.hpp"

#include <gtest/gtest.h>

#include <variant>

namespace {

TEST(CommandParserTest, ParsesWindowCaptureAndTrimsFollowUpAction) {
  qingying::CommandParser parser;
  qingying::ActionRequest request;

  ASSERT_TRUE(parser.tryParse(L"截取微信窗口并复制", &request));
  EXPECT_EQ(request.type(), qingying::ActionType::CaptureWindow);
  const auto* payload = std::get_if<qingying::CaptureWindowRequest>(&request.payload);
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->window_query, L"微信");
}

TEST(CommandParserTest, ParsesExplicitWindowCaptureWithQuotedTitle) {
  qingying::CommandParser parser;
  qingying::ActionRequest request;

  ASSERT_TRUE(parser.tryParse(L"截图窗口：\"QingYing\"", &request));
  const auto* payload = std::get_if<qingying::CaptureWindowRequest>(&request.payload);
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->window_query, L"QingYing");
}

TEST(CommandParserTest, ParsesCenteredCropWithChineseMultiplySign) {
  qingying::CommandParser parser;
  qingying::ActionRequest request;

  ASSERT_TRUE(parser.tryParse(L"截取屏幕中心 640 × 480", &request));
  EXPECT_EQ(request.type(), qingying::ActionType::CropCenter);
  const auto* payload = std::get_if<qingying::CropCenterRequest>(&request.payload);
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->width, 640);
  EXPECT_EQ(payload->height, 480);
}

TEST(CommandParserTest, ParsesCopyPinStatusAndSaveActions) {
  qingying::CommandParser parser;
  qingying::ActionRequest request;

  ASSERT_TRUE(parser.tryParse(L"复制当前截图", &request));
  EXPECT_EQ(request.type(), qingying::ActionType::Copy);
  ASSERT_TRUE(parser.tryParse(L"钉图", &request));
  EXPECT_EQ(request.type(), qingying::ActionType::Pin);
  ASSERT_TRUE(parser.tryParse(L"查看状态", &request));
  EXPECT_EQ(request.type(), qingying::ActionType::Status);
  ASSERT_TRUE(parser.tryParse(L"保存到 C:\\temp\\capture.png", &request));
  const auto* payload = std::get_if<qingying::SaveRequest>(&request.payload);
  ASSERT_NE(payload, nullptr);
  EXPECT_EQ(payload->path, L"C:\\temp\\capture.png");
}

TEST(CommandParserTest, RejectsIncompleteUnsupportedAndNullOutputWithoutMutation) {
  qingying::CommandParser parser;
  qingying::ActionRequest request =
      qingying::makeActionRequest(qingying::StatusRequest{});

  EXPECT_FALSE(parser.tryParse(L"截取窗口", &request));
  EXPECT_EQ(request.type(), qingying::ActionType::Status);
  EXPECT_FALSE(parser.tryParse(L"长截图", &request));
  EXPECT_FALSE(parser.tryParse(L"保存", &request));
  EXPECT_FALSE(parser.tryParse(L"截取中心 0x100", &request));
  EXPECT_FALSE(parser.tryParse(L"复制", nullptr));
}

}  // namespace
