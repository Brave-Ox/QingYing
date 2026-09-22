#include "qingying/command/command_parser.hpp"

#include <gtest/gtest.h>

#include <array>

namespace {

struct GoldenPlanCase {
  const wchar_t* input;
  std::array<qingying::ActionType, 2> expected_types;
  std::size_t action_count;
};

TEST(CommandParserP0RegressionTest, ExistingAcceptedUtterancesRemainExactlyCompatible) {
  const GoldenPlanCase cases[] = {
      {L"复制", {qingying::ActionType::Copy, qingying::ActionType::Status}, 1},
      {L"复制当前截图", {qingying::ActionType::Copy, qingying::ActionType::Status}, 1},
      {L"钉图", {qingying::ActionType::Pin, qingying::ActionType::Status}, 1},
      {L"钉住当前截图", {qingying::ActionType::Pin, qingying::ActionType::Status}, 1},
      {L"状态", {qingying::ActionType::Status, qingying::ActionType::Status}, 1},
      {L"查看状态", {qingying::ActionType::Status, qingying::ActionType::Status}, 1},
      {L"保存 C:\\temp\\a.png", {qingying::ActionType::Save, qingying::ActionType::Status}, 1},
      {L"保存为：\"C:\\temp\\a b.png\"", {qingying::ActionType::Save, qingying::ActionType::Status}, 1},
      {L"保存到 C:\\temp\\a.png", {qingying::ActionType::Save, qingying::ActionType::Status}, 1},
      {L"中心裁剪 640x480", {qingying::ActionType::CropCenter, qingying::ActionType::Status}, 1},
      {L"截取屏幕中间 640 X 480", {qingying::ActionType::CropCenter, qingying::ActionType::Status}, 1},
      {L"裁切 640 * 480", {qingying::ActionType::CropCenter, qingying::ActionType::Status}, 1},
      {L"截取窗口：微信", {qingying::ActionType::CaptureWindow, qingying::ActionType::Status}, 1},
      {L"截图窗口：\"QingYing\"", {qingying::ActionType::CaptureWindow, qingying::ActionType::Status}, 1},
      {L"截屏窗口：资源管理器", {qingying::ActionType::CaptureWindow, qingying::ActionType::Status}, 1},
      {L"截取微信窗口", {qingying::ActionType::CaptureWindow, qingying::ActionType::Status}, 1},
      {L"截取微信窗口并复制", {qingying::ActionType::CaptureWindow, qingying::ActionType::Copy}, 2},
      {L"截取微信窗口并钉图", {qingying::ActionType::CaptureWindow, qingying::ActionType::Pin}, 2},
  };

  qingying::CommandParser parser;
  for (const GoldenPlanCase& item : cases) {
    qingying::CommandPlan plan;
    ASSERT_TRUE(parser.tryParsePlan(item.input, &plan)) << item.input;
    ASSERT_EQ(plan.actions.size(), item.action_count) << item.input;
    for (std::size_t index = 0; index < item.action_count; ++index) {
      EXPECT_EQ(plan.actions[index].type(), item.expected_types[index]) << item.input;
    }
  }
}

TEST(CommandParserP0RegressionTest, RejectedUtterancesNeverMutatePlanOrRequest) {
  const wchar_t* rejected_inputs[] = {
      L"", L" \t\r\n ", L"保存", L"保存到：\"\"", L"截取窗口",
      L"截取窗口并复制", L"长截图", L"中心裁剪 0x100",
      L"未知命令",
  };

  qingying::CommandParser parser;
  for (const wchar_t* input : rejected_inputs) {
    qingying::CommandPlan plan;
    plan.actions.push_back(qingying::makeActionRequest(qingying::StatusRequest{}));
    qingying::ActionRequest request =
        qingying::makeActionRequest(qingying::StatusRequest{});

    EXPECT_FALSE(parser.tryParsePlan(input, &plan)) << input;
    ASSERT_EQ(plan.actions.size(), 1u) << input;
    EXPECT_EQ(plan.actions.front().type(), qingying::ActionType::Status) << input;
    EXPECT_FALSE(parser.tryParse(input, &request)) << input;
    EXPECT_EQ(request.type(), qingying::ActionType::Status) << input;
  }
}

}  // namespace
