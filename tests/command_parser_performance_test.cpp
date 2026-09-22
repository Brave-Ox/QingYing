#include "qingying/command/command_parser.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;

std::string measureAverageNanoseconds(const qingying::CommandParser& parser,
                                      const std::wstring& input) {
  constexpr int kWarmupIterations = 100;
  constexpr int kMeasuredIterations = 10000;
  qingying::CommandPlan plan;
  for (int index = 0; index < kWarmupIterations; ++index) {
    static_cast<void>(parser.tryParsePlan(input, &plan));
  }

  const Clock::time_point started = Clock::now();
  for (int index = 0; index < kMeasuredIterations; ++index) {
    EXPECT_TRUE(parser.tryParsePlan(input, &plan));
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
      Clock::now() - started);
  return std::to_string(elapsed.count() / kMeasuredIterations);
}

TEST(CommandParserPerformanceTest, RecordsStableLocalParseBaselines) {
  const qingying::CommandParser parser;
  const std::array<std::pair<const char*, const wchar_t*>, 3> inputs = {{
      {"short_success_average_ns", L"复制"},
      {"two_action_success_average_ns", L"截取微信窗口并复制"},
      {"long_success_average_ns", L"保存为：\"C:\\temp\\qingying-capture.png\""},
  }};

  for (const auto& input : inputs) {
    const std::string average = measureAverageNanoseconds(parser, input.second);
    RecordProperty(input.first, average);
    EXPECT_FALSE(average.empty());
  }
}

}  // namespace
