#include "qingying/annotate/color_picker_state.hpp"

#include <gtest/gtest.h>

namespace qingying {
namespace {

TEST(ColorPickerStateTest, OpenCopiesCommittedIntoDraft)
{
  ColorPickerState state;
  const ColorBgra gold{99, 195, 227, 200};
  state.open(gold);
  EXPECT_EQ(state.committed().r, gold.r);
  EXPECT_EQ(state.draft().b, gold.b);
  EXPECT_EQ(state.draft().a, gold.a);
  EXPECT_EQ(state.format(), ColorPickerFormat::Hex);
}

TEST(ColorPickerStateTest, SwitchingFormatKeepsDraft)
{
  ColorPickerState state;
  state.open(ColorBgra{0, 0, 220, 255});
  state.setFormat(ColorPickerFormat::Hsv);
  EXPECT_EQ(state.format(), ColorPickerFormat::Hsv);
  EXPECT_EQ(state.draft().r, 220);
  state.setFormat(ColorPickerFormat::Rgb);
  EXPECT_EQ(state.draft().r, 220);
}

TEST(ColorPickerStateTest, ConfirmReturnsDraftCancelLeavesCommitted)
{
  ColorPickerState state;
  const ColorBgra start{0, 0, 220, 255};
  state.open(start);
  state.setRgb(10, 20, 30);
  state.setAlpha(128);
  EXPECT_EQ(state.confirm().r, 10);
  EXPECT_EQ(state.confirm().a, 128);
  EXPECT_EQ(state.committed().r, start.r);
  EXPECT_EQ(state.committed().a, start.a);
}

TEST(ColorPickerStateTest, BlackValueChangeKeepsHue)
{
  ColorPickerState state;
  state.open(hsvToRgb(45, 93, 80, 255));
  const int hue = state.hueDegrees();
  EXPECT_NEAR(hue, 45, 2);
  state.setSaturationValue(93, 0);
  EXPECT_EQ(state.hueDegrees(), hue);
  state.setSaturationValue(93, 33);
  EXPECT_EQ(state.hueDegrees(), hue);
  const ColorHsv hsv = rgbToHsv(state.draft());
  EXPECT_NEAR(hsv.hue_degrees, hue, 2);
}

TEST(ColorPickerStateTest, SampleCanvasSetsOpaqueRgb)
{
  Image image;
  image.width = 2;
  image.height = 1;
  image.pixels = {packColorBgra(ColorBgra{16, 32, 64, 255}),
                  packColorBgra(ColorBgra{1, 2, 3, 255})};

  ColorBgra sampled{};
  ASSERT_TRUE(sampleImageBgra(image, 0, 0, sampled));
  ColorPickerState state;
  state.open(ColorBgra{0, 0, 220, 80});
  state.sampleOpaqueRgb(sampled);
  EXPECT_EQ(state.draft().r, 64);
  EXPECT_EQ(state.draft().g, 32);
  EXPECT_EQ(state.draft().b, 16);
  EXPECT_EQ(state.draft().a, 255);
  EXPECT_FALSE(sampleImageBgra(image, 2, 0, sampled));
}

}  // namespace
}  // namespace qingying
