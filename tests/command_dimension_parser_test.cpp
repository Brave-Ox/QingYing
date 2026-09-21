#include "command_dimension_parser.hpp"

#include <gtest/gtest.h>

namespace {

TEST(CommandDimensionParserTest, AcceptsTheExistingDimensionSeparators) {
  using qingying::command_detail::Dimensions;
  using qingying::command_detail::parseCenteredCropDimensions;
  for (const wchar_t* input : {L"中心裁剪 640x480", L"中心裁剪 640 X 480",
                               L"中心裁剪 640 × 480", L"中心裁剪 640 * 480"}) {
    Dimensions dimensions;
    ASSERT_TRUE(parseCenteredCropDimensions(input, &dimensions)) << input;
    EXPECT_EQ(dimensions.width, 640);
    EXPECT_EQ(dimensions.height, 480);
  }
}

TEST(CommandDimensionParserTest, RejectsZeroAndMissingIntent) {
  using qingying::command_detail::Dimensions;
  using qingying::command_detail::parseCenteredCropDimensions;
  for (const wchar_t* input : {L"中心裁剪 0x480", L"中心裁剪 640x0", L"640x480"}) {
    Dimensions dimensions;
    EXPECT_FALSE(parseCenteredCropDimensions(input, &dimensions)) << input;
  }
}

TEST(CommandDimensionParserTest, PreservesLegacySearchAfterAnOverflowPrefix) {
  using qingying::command_detail::Dimensions;
  using qingying::command_detail::parseCenteredCropDimensions;
  Dimensions dimensions;
  ASSERT_TRUE(parseCenteredCropDimensions(L"中心裁剪 999999999999x1", &dimensions));
  EXPECT_EQ(dimensions.width, 999999999);
  EXPECT_EQ(dimensions.height, 1);
}

}  // namespace
