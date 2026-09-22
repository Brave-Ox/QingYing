#include "qingying/command/command_parser.hpp"
#include "qingying/command/command_plan_explainer.hpp"
#include "qingying/command/command_plan_validator.hpp"

#include <gtest/gtest.h>

namespace {

TEST(CommandParserDiagnosticsTest, SuccessfulParseReportsRuleAndReadablePlan) {
  qingying::CommandParser parser;
  qingying::CommandPlan plan;
  qingying::CommandParseDiagnostic diagnostic;
  ASSERT_TRUE(parser.tryParsePlan(L"截取微信窗口并复制", &plan, &diagnostic));
  EXPECT_TRUE(diagnostic.succeeded);
  EXPECT_EQ(diagnostic.stage, qingying::CommandParseStage::Complete);
  EXPECT_EQ(diagnostic.matched_rule, L"capture-window");
  EXPECT_EQ(qingying::explainCommandPlan(plan), L"截取窗口：微信；然后复制当前截图");
  EXPECT_TRUE(qingying::validateCommandPlan(plan));
}

TEST(CommandParserDiagnosticsTest, FailedParseDoesNotMutateOutputPlan) {
  qingying::CommandParser parser;
  qingying::CommandPlan plan;
  plan.actions.push_back(qingying::makeActionRequest(qingying::StatusRequest{}));
  qingying::CommandParseDiagnostic diagnostic;
  EXPECT_FALSE(parser.tryParsePlan(L"保存", &plan, &diagnostic));
  ASSERT_EQ(plan.actions.size(), 1u);
  EXPECT_EQ(plan.actions.front().type(), qingying::ActionType::Status);
  EXPECT_EQ(diagnostic.stage, qingying::CommandParseStage::MatchRule);
  EXPECT_FALSE(diagnostic.succeeded);
}

TEST(CommandParserDiagnosticsTest, FailureStagesAndInputMetadataStayCompatible) {
  qingying::CommandParser parser;
  qingying::CommandPlan plan;
  qingying::CommandParseDiagnostic diagnostic;

  EXPECT_FALSE(parser.tryParsePlan(L"  \t\r\n  ", &plan, &diagnostic));
  EXPECT_EQ(diagnostic.stage, qingying::CommandParseStage::Normalize);
  EXPECT_EQ(diagnostic.input_length, 7u);
  EXPECT_TRUE(diagnostic.normalized_input.empty());
  EXPECT_TRUE(diagnostic.matched_rule.empty());
  EXPECT_FALSE(diagnostic.succeeded);

  EXPECT_FALSE(parser.tryParsePlan(L"未知命令", &plan, &diagnostic));
  EXPECT_EQ(diagnostic.stage, qingying::CommandParseStage::MatchRule);
  EXPECT_EQ(diagnostic.normalized_input, L"未知命令");
  EXPECT_TRUE(diagnostic.matched_rule.empty());
  EXPECT_FALSE(diagnostic.succeeded);

  EXPECT_FALSE(parser.tryParsePlan(L"复制", nullptr, &diagnostic));
  EXPECT_EQ(diagnostic.stage, qingying::CommandParseStage::Normalize);
  EXPECT_EQ(diagnostic.input_length, 2u);
  EXPECT_EQ(diagnostic.message, L"output is null");
  EXPECT_FALSE(diagnostic.succeeded);
}

TEST(CommandPlanValidatorTest, RejectsUnexpectedFollowUpWithoutChangingParserRules) {
  qingying::CommandPlan invalid;
  invalid.actions.push_back(
      qingying::makeActionRequest(qingying::StatusRequest{}));
  invalid.actions.push_back(qingying::makeActionRequest(
      qingying::CopyRequest{qingying::ResultSelection::current()}));
  const auto result = qingying::validateCommandPlan(invalid);
  EXPECT_EQ(result.code, qingying::CommandPlanValidationCode::InvalidFollowUp);
  EXPECT_EQ(result.action_index, 1u);
}

}  // namespace
