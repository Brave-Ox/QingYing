#include "qingying/command/command_parser.hpp"

#include <gtest/gtest.h>

#include <variant>

namespace {

struct GoldenCase {
  const wchar_t* input;
  qingying::ActionType primary;
  std::size_t action_count;
};

TEST(CommandParserGoldenTest, ExistingAcceptedUtterancesKeepTheirPlanShape) {
  const GoldenCase cases[] = {
      {L"复制", qingying::ActionType::Copy, 1},
      {L"钉住当前截图", qingying::ActionType::Pin, 1},
      {L"查看状态", qingying::ActionType::Status, 1},
      {L"保存到 C:\\temp\\capture.png", qingying::ActionType::Save, 1},
      {L"截取屏幕中心 640 × 480", qingying::ActionType::CropCenter, 1},
      {L"截图窗口：\"QingYing\"", qingying::ActionType::CaptureWindow, 1},
      {L"截取微信窗口并复制", qingying::ActionType::CaptureWindow, 2},
      {L"截取微信窗口并钉图", qingying::ActionType::CaptureWindow, 2},
  };
  qingying::CommandParser parser;
  for (const GoldenCase& item : cases) {
    qingying::CommandPlan plan;
    ASSERT_TRUE(parser.tryParsePlan(item.input, &plan)) << item.input;
    ASSERT_EQ(plan.actions.size(), item.action_count) << item.input;
    EXPECT_EQ(plan.actions.front().type(), item.primary) << item.input;
  }
}

TEST(CommandParserGoldenTest, ExistingRejectedUtterancesStayRejected) {
  qingying::CommandParser parser;
  for (const wchar_t* input : {L"", L"保存", L"截取窗口", L"长截图",
                               L"截取中心 0x100", L"未知命令"}) {
    qingying::CommandPlan plan;
    EXPECT_FALSE(parser.tryParsePlan(input, &plan)) << input;
  }
}

}  // namespace
