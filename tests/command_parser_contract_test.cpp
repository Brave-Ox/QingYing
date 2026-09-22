#include "qingying/command/command_parser.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <variant>

namespace {

TEST(CommandParserContractTest, ParsesPayloadDetailsWithoutAddingSyntax) {
  qingying::CommandParser parser;
  qingying::CommandPlan plan;

  ASSERT_TRUE(parser.tryParsePlan(L"保存为：\"C:\\temp\\a b.png\"", &plan));
  ASSERT_EQ(plan.actions.size(), 1u);
  const auto* save = std::get_if<qingying::SaveRequest>(&plan.actions.front().payload);
  ASSERT_NE(save, nullptr);
  EXPECT_EQ(save->path, L"C:\\temp\\a b.png");
  EXPECT_EQ(save->result.kind, qingying::ResultSelectionKind::Current);
  EXPECT_EQ(save->result.result_id, qingying::kInvalidResultId);
  EXPECT_FALSE(save->overwrite);

  ASSERT_TRUE(parser.tryParsePlan(L"截图窗口：\"QingYing\"", &plan));
  ASSERT_EQ(plan.actions.size(), 1u);
  const auto* capture =
      std::get_if<qingying::CaptureWindowRequest>(&plan.actions.front().payload);
  ASSERT_NE(capture, nullptr);
  EXPECT_EQ(capture->window_query, L"QingYing");
  EXPECT_EQ(capture->match, qingying::WindowMatchMode::Contains);
  EXPECT_EQ(capture->process_id, std::nullopt);

  ASSERT_TRUE(parser.tryParsePlan(L"中心裁剪 640×480", &plan));
  ASSERT_EQ(plan.actions.size(), 1u);
  const auto* crop =
      std::get_if<qingying::CropCenterRequest>(&plan.actions.front().payload);
  ASSERT_NE(crop, nullptr);
  EXPECT_EQ(crop->width, 640);
  EXPECT_EQ(crop->height, 480);
}

TEST(CommandParserContractTest, ResultActionsRetainCurrentSelectionSemantics) {
  struct Case {
    const wchar_t* input;
    qingying::ActionType type;
  };
  const Case cases[] = {
      {L"复制", qingying::ActionType::Copy},
      {L"钉图", qingying::ActionType::Pin},
      {L"截取微信窗口并复制", qingying::ActionType::Copy},
      {L"截取微信窗口并钉图", qingying::ActionType::Pin},
  };

  qingying::CommandParser parser;
  for (const Case& item : cases) {
    qingying::CommandPlan plan;
    ASSERT_TRUE(parser.tryParsePlan(item.input, &plan)) << item.input;
    const qingying::ActionRequest& action =
        plan.actions[item.type == qingying::ActionType::Copy ||
                             item.type == qingying::ActionType::Pin
                         ? plan.actions.size() - 1
                         : 0];
    ASSERT_EQ(action.type(), item.type) << item.input;

    if (const auto* copy = std::get_if<qingying::CopyRequest>(&action.payload)) {
      EXPECT_EQ(copy->result.kind, qingying::ResultSelectionKind::Current);
      EXPECT_EQ(copy->result.result_id, qingying::kInvalidResultId);
    } else {
      const auto* pin = std::get_if<qingying::PinRequest>(&action.payload);
      ASSERT_NE(pin, nullptr);
      EXPECT_EQ(pin->result.kind, qingying::ResultSelectionKind::Current);
      EXPECT_EQ(pin->result.result_id, qingying::kInvalidResultId);
    }
  }
}

TEST(CommandParserContractTest, LegacySingleActionApiUsesThePrimaryPlanAction) {
  const wchar_t* cases[] = {
      L"复制", L"保存到 C:\\temp\\capture.png", L"中心裁剪 320x240",
      L"截取微信窗口并复制", L"截取微信窗口并钉图",
  };

  qingying::CommandParser parser;
  for (const wchar_t* input : cases) {
    qingying::CommandPlan plan;
    qingying::ActionRequest action;
    ASSERT_TRUE(parser.tryParsePlan(input, &plan)) << input;
    ASSERT_TRUE(parser.tryParse(input, &action)) << input;
    ASSERT_FALSE(plan.actions.empty()) << input;
    EXPECT_EQ(action.type(), plan.actions.front().type()) << input;
    EXPECT_EQ(action.payload.index(), plan.actions.front().payload.index()) << input;
  }
}

}  // namespace
