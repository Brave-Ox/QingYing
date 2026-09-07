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

TEST(ColorPickerStateTest, SampleCanvasPointUsesEditorImageOrigin)
{
  Image image;
  image.width = 2;
  image.height = 2;
  image.pixels = {
      packColorBgra(ColorBgra{10, 20, 30, 255}),
      packColorBgra(ColorBgra{40, 50, 60, 255}),
      packColorBgra(ColorBgra{70, 80, 90, 255}),
      packColorBgra(ColorBgra{100, 110, 120, 255}),
  };

  ColorBgra sampled{};
  ASSERT_TRUE(sampleImageBgraAtClientPoint(image, 11, 23, 10, 22, sampled));
  EXPECT_EQ(sampled.r, 120);
  EXPECT_EQ(sampled.g, 110);
  EXPECT_EQ(sampled.b, 100);
  EXPECT_FALSE(sampleImageBgraAtClientPoint(image, 10, 24, 10, 22, sampled));
}

TEST(ColorPickerStateTest, SampleAtClientPointUpdatesDraftOnlyForImagePixels)
{
  Image image;
  image.width = 2;
  image.height = 1;
  image.pixels = {
      packColorBgra(ColorBgra{10, 20, 30, 255}),
      packColorBgra(ColorBgra{40, 50, 60, 255}),
  };

  ColorPickerState state;
  state.open(ColorBgra{1, 2, 3, 80});

  ASSERT_TRUE(sampleColorPickerAtClientPoint(state, image, 31, 40, 30, 40));
  EXPECT_EQ(state.draft().r, 60);
  EXPECT_EQ(state.draft().g, 50);
  EXPECT_EQ(state.draft().b, 40);
  EXPECT_EQ(state.draft().a, 255);

  EXPECT_FALSE(sampleColorPickerAtClientPoint(state, image, 32, 40, 30, 40));
  EXPECT_EQ(state.draft().r, 60);
  EXPECT_EQ(state.draft().g, 50);
  EXPECT_EQ(state.draft().b, 40);
}

TEST(ColorPickerStateTest, Win32ColorRefConvertsToOpaqueBgra)
{
  ColorBgra color{};
  ASSERT_TRUE(win32ColorRefToBgra(0x00A1B2C3u, color));
  EXPECT_EQ(color.r, 0xC3);
  EXPECT_EQ(color.g, 0xB2);
  EXPECT_EQ(color.b, 0xA1);
  EXPECT_EQ(color.a, 0xFF);

  EXPECT_FALSE(win32ColorRefToBgra(0xFFFFFFFFu, color));
}

TEST(ColorPickerStateTest, SamplesVirtualDesktopSnapshotByScreenCoordinates)
{
  Image snapshot;
  snapshot.width = 2;
  snapshot.height = 2;
  snapshot.pixels = {packColorBgra(ColorBgra{1, 2, 3, 255}),
                     packColorBgra(ColorBgra{4, 5, 6, 255}),
                     packColorBgra(ColorBgra{7, 8, 9, 255}),
                     packColorBgra(ColorBgra{10, 11, 12, 255})};

  ColorBgra sampled{};
  ASSERT_TRUE(sampleImageBgraAtScreenPoint(snapshot, -100, 50, -99, 51,
                                           sampled));
  EXPECT_EQ(sampled.r, 12);
  EXPECT_EQ(sampled.g, 11);
  EXPECT_EQ(sampled.b, 10);
  EXPECT_FALSE(sampleImageBgraAtScreenPoint(snapshot, -100, 50, -101, 51,
                                            sampled));
}

}  // namespace
}  // namespace qingying
