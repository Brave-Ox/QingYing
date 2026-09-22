#include "qingying/command/command_parser.hpp"

#include <gtest/gtest.h>

namespace {

TEST(CommandParserFuzzTest, MalformedUnicodeLikeInputsNeverMutateFailedOutput) {
  qingying::CommandParser parser;
  const std::wstring cases[] = {
      std::wstring(4096, L' '), L"\"\"\"", L"保存：\0", L"截取窗口并复制",
      L"中心裁剪 １２x４８", L"中心裁剪 1e3x10", L"截取窗口：‘微信’",
      L"截图窗口：\"未闭合", L"截取😀窗口", L"保存到 \t\r\n",
      L"截取窗口：\"\"", L"中心裁剪 2147483648x1",
      L"中心裁剪 1x2147483648", L"中心裁剪 1xx2",
      L"中心裁剪 1\t×\t0", L"截取窗口：：：", L"复制并钉图",
      std::wstring(4096, L'9') + L"x1", L"\u2003\u2003\u2003"};
  for (const std::wstring& input : cases) {
    qingying::CommandPlan plan;
    plan.actions.push_back(qingying::makeActionRequest(qingying::StatusRequest{}));
    qingying::ActionRequest request =
        qingying::makeActionRequest(qingying::StatusRequest{});
    if (!parser.tryParsePlan(input, &plan)) {
      ASSERT_EQ(plan.actions.size(), 1u);
      EXPECT_EQ(plan.actions.front().type(), qingying::ActionType::Status);
    }
    if (!parser.tryParse(input, &request)) {
      EXPECT_EQ(request.type(), qingying::ActionType::Status);
    }
  }
}

}  // namespace
