#include "qingying/command/command_plan_explainer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace {

using qingying::ActionType;
using qingying::CommandPlan;

CommandPlan makeSingleActionPlan(qingying::ActionRequest action) {
  CommandPlan plan;
  plan.actions.push_back(std::move(action));
  return plan;
}

TEST(CommandPlanExplainerTest, EmptyPlanHasNoPresentationText) {
  EXPECT_TRUE(qingying::explainCommandPlan(CommandPlan{}).empty());
}

TEST(CommandPlanExplainerTest, EachSupportedActionHasStablePresentationText) {
  const std::pair<CommandPlan, std::wstring> cases[] = {
      {makeSingleActionPlan(qingying::makeActionRequest(
           qingying::CaptureWindowRequest{L"微信"})),
       L"截取窗口：微信"},
      {makeSingleActionPlan(qingying::makeActionRequest(
           qingying::CropCenterRequest{640, 480})),
       L"中心裁剪：640×480"},
      {makeSingleActionPlan(qingying::makeActionRequest(
           qingying::CopyRequest{qingying::ResultSelection::current()})),
       L"复制当前截图"},
      {makeSingleActionPlan(qingying::makeActionRequest(
           qingying::SaveRequest{qingying::ResultSelection::current(), L"C:\\temp\\a.png"})),
       L"保存到：C:\\temp\\a.png"},
      {makeSingleActionPlan(qingying::makeActionRequest(
           qingying::PinRequest{qingying::ResultSelection::current()})),
       L"钉图"},
      {makeSingleActionPlan(qingying::makeActionRequest(qingying::StatusRequest{})),
       L"查看状态"},
  };

  for (const auto& item : cases) {
    EXPECT_EQ(qingying::explainCommandPlan(item.first), item.second);
  }
}

TEST(CommandPlanExplainerTest, OrderedActionsUseTheSameConnectorAsTheUiPreview) {
  CommandPlan plan;
  plan.actions.push_back(qingying::makeActionRequest(
      qingying::CaptureWindowRequest{L"资源管理器"}));
  plan.actions.push_back(qingying::makeActionRequest(
      qingying::PinRequest{qingying::ResultSelection::current()}));

  ASSERT_EQ(plan.actions.front().type(), ActionType::CaptureWindow);
  ASSERT_EQ(plan.actions.back().type(), ActionType::Pin);
  EXPECT_EQ(qingying::explainCommandPlan(plan), L"截取窗口：资源管理器；然后钉图");
}

}  // namespace
