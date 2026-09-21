#include "command_text.hpp"

#include <gtest/gtest.h>

namespace {

TEST(CommandTextTest, TrimmingQuotesAndPunctuationKeepLegacySemantics) {
  using namespace qingying::command_detail;
  EXPECT_EQ(trim(L"  微信  "), L"微信");
  EXPECT_EQ(stripLeadingPunctuation(L"：，  C:\\temp\\a.png"),
            L"C:\\temp\\a.png");
  EXPECT_EQ(stripQuotes(L"  “微信”  "), L"微信");
  EXPECT_EQ(normalize(L"  截图窗口：微信  "), L"截图窗口：微信");
}

TEST(CommandTextTest, PrefixAndSuffixChecksAreExact) {
  using namespace qingying::command_detail;
  EXPECT_TRUE(startsWith(L"截取微信窗口", L"截取"));
  EXPECT_TRUE(endsWith(L"截取微信窗口并复制", L"并复制"));
  EXPECT_FALSE(startsWith(L"截图微信窗口", L"截取"));
  EXPECT_FALSE(endsWith(L"复制当前截图", L"并复制"));
}

}  // namespace
