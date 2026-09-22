#include "command_rules.hpp"

#include <gtest/gtest.h>

namespace {

TEST(CommandRulesTest, FixedAliasesMapToTheirStableRuleAndAction) {
  using qingying::ActionType;
  using qingying::command_detail::RuleMatch;
  using qingying::command_detail::RuleParseResult;
  using qingying::command_detail::parseRule;

  struct Case {
    const wchar_t* input;
    RuleMatch rule;
    ActionType action;
  };
  const Case cases[] = {
      {L"复制", RuleMatch::Copy, ActionType::Copy},
      {L"复制当前截图", RuleMatch::Copy, ActionType::Copy},
      {L"钉图", RuleMatch::Pin, ActionType::Pin},
      {L"钉住当前截图", RuleMatch::Pin, ActionType::Pin},
      {L"状态", RuleMatch::Status, ActionType::Status},
      {L"查看状态", RuleMatch::Status, ActionType::Status},
  };

  for (const Case& item : cases) {
    RuleParseResult result;
    ASSERT_TRUE(parseRule(item.input, &result)) << item.input;
    EXPECT_EQ(result.match, item.rule) << item.input;
    EXPECT_EQ(result.primary.type(), item.action) << item.input;
    EXPECT_FALSE(result.has_follow_up) << item.input;
  }
}

TEST(CommandRulesTest, WindowFollowUpsKeepLegacyPlanShape) {
  using qingying::ActionType;
  using qingying::command_detail::RuleMatch;
  using qingying::command_detail::RuleParseResult;
  using qingying::command_detail::parseRule;

  struct Case {
    const wchar_t* input;
    bool has_follow_up;
    ActionType follow_up;
  };
  const Case cases[] = {
      {L"截取微信窗口", false, ActionType::Status},
      {L"截取微信窗口并复制", true, ActionType::Copy},
      {L"截取微信窗口并钉图", true, ActionType::Pin},
      // "并保存" was accepted by the legacy parser as a capture-only command.
      {L"截取微信窗口并保存", false, ActionType::Status},
  };

  for (const Case& item : cases) {
    RuleParseResult result;
    ASSERT_TRUE(parseRule(item.input, &result)) << item.input;
    EXPECT_EQ(result.match, RuleMatch::CaptureWindow) << item.input;
    EXPECT_EQ(result.primary.type(), ActionType::CaptureWindow) << item.input;
    EXPECT_EQ(result.has_follow_up, item.has_follow_up) << item.input;
    if (item.has_follow_up) {
      EXPECT_EQ(result.follow_up.type(), item.follow_up) << item.input;
    }
  }
}

TEST(CommandRulesTest, FailedRuleMatchDoesNotOverwriteTheCallerResult) {
  using qingying::ActionType;
  using qingying::ResultSelection;
  using qingying::StatusRequest;
  using qingying::command_detail::RuleMatch;
  using qingying::command_detail::RuleParseResult;
  using qingying::command_detail::parseRule;

  RuleParseResult result;
  result.match = RuleMatch::Status;
  result.primary = qingying::makeActionRequest(StatusRequest{});
  result.follow_up = qingying::makeActionRequest(
      qingying::CopyRequest{ResultSelection::current()});
  result.has_follow_up = true;

  EXPECT_FALSE(parseRule(L"未知命令", &result));
  EXPECT_EQ(result.match, RuleMatch::Status);
  EXPECT_EQ(result.primary.type(), ActionType::Status);
  EXPECT_TRUE(result.has_follow_up);
  EXPECT_EQ(result.follow_up.type(), ActionType::Copy);
}

TEST(CommandRulesTest, RuleNamesRemainStableForDiagnostics) {
  using qingying::command_detail::RuleMatch;
  using qingying::command_detail::ruleMatchName;

  EXPECT_STREQ(ruleMatchName(RuleMatch::None), L"none");
  EXPECT_STREQ(ruleMatchName(RuleMatch::Copy), L"copy");
  EXPECT_STREQ(ruleMatchName(RuleMatch::Pin), L"pin");
  EXPECT_STREQ(ruleMatchName(RuleMatch::Status), L"status");
  EXPECT_STREQ(ruleMatchName(RuleMatch::Save), L"save");
  EXPECT_STREQ(ruleMatchName(RuleMatch::CropCenter), L"crop-center");
  EXPECT_STREQ(ruleMatchName(RuleMatch::CaptureWindow), L"capture-window");
}

}  // namespace
