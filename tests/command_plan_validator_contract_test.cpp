#include "qingying/command/command_plan_validator.hpp"

#include <gtest/gtest.h>

#include <utility>

namespace {

qingying::ActionRequest captureWindow() {
  return qingying::makeActionRequest(qingying::CaptureWindowRequest{L"微信"});
}

qingying::ActionRequest copyCurrent() {
  return qingying::makeActionRequest(
      qingying::CopyRequest{qingying::ResultSelection::current()});
}

qingying::ActionRequest pinCurrent() {
  return qingying::makeActionRequest(
      qingying::PinRequest{qingying::ResultSelection::current()});
}

TEST(CommandPlanValidatorContractTest, AcceptsEverySupportedSingleActionShape) {
  qingying::ActionRequest actions[] = {
      qingying::makeActionRequest(qingying::StatusRequest{}),
      captureWindow(),
      qingying::makeActionRequest(qingying::CropCenterRequest{320, 240}),
      copyCurrent(),
      qingying::makeActionRequest(qingying::SaveRequest{
          qingying::ResultSelection::current(), L"C:\\temp\\capture.png"}),
      pinCurrent(),
  };

  for (const qingying::ActionRequest& action : actions) {
    qingying::CommandPlan plan;
    plan.actions.push_back(action);
    const auto result = qingying::validateCommandPlan(plan);
    EXPECT_TRUE(result);
    EXPECT_EQ(result.code, qingying::CommandPlanValidationCode::Ok);
    EXPECT_EQ(result.action_index, 0u);
  }
}

TEST(CommandPlanValidatorContractTest, AcceptsOnlyCaptureCopyOrPinAsTwoActionPlans) {
  for (qingying::ActionRequest follow_up : {copyCurrent(), pinCurrent()}) {
    qingying::CommandPlan plan;
    plan.actions.push_back(captureWindow());
    plan.actions.push_back(std::move(follow_up));
    const auto result = qingying::validateCommandPlan(plan);
    EXPECT_TRUE(result);
    EXPECT_EQ(result.code, qingying::CommandPlanValidationCode::Ok);
  }
}

TEST(CommandPlanValidatorContractTest, ReportsTheFirstInvalidPlanBoundary) {
  qingying::CommandPlan invalid_follow_up;
  invalid_follow_up.actions.push_back(captureWindow());
  invalid_follow_up.actions.push_back(qingying::makeActionRequest(
      qingying::SaveRequest{qingying::ResultSelection::current(), L"C:\\temp\\a.png"}));
  const auto invalid_follow_up_result =
      qingying::validateCommandPlan(invalid_follow_up);
  EXPECT_EQ(invalid_follow_up_result.code,
            qingying::CommandPlanValidationCode::InvalidFollowUp);
  EXPECT_EQ(invalid_follow_up_result.action_index, 1u);

  qingying::CommandPlan too_many;
  too_many.actions.push_back(captureWindow());
  too_many.actions.push_back(copyCurrent());
  too_many.actions.push_back(pinCurrent());
  const auto too_many_result = qingying::validateCommandPlan(too_many);
  EXPECT_EQ(too_many_result.code, qingying::CommandPlanValidationCode::TooManyActions);
  EXPECT_EQ(too_many_result.action_index, 2u);
}

}  // namespace
