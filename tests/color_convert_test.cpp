#include "qingying/annotate/color_convert.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

namespace qingying {
namespace {

TEST(ColorConvertTest, FormatHexWritesLowercaseRrggbb)
{
  wchar_t text[ColorHexTextMaxChars]{};
  ASSERT_TRUE(formatHex(ColorBgra{99, 195, 227, 255}, text, ColorHexTextMaxChars));
  EXPECT_STREQ(text, L"#e3c363");
}

TEST(ColorConvertTest, ParseHexAcceptsShortAndLongForms)
{
  ColorBgra color{1, 2, 3, 200};
  ASSERT_TRUE(parseHex(L"#e3c363", color));
  EXPECT_EQ(color.r, 227);
  EXPECT_EQ(color.g, 195);
  EXPECT_EQ(color.b, 99);
  EXPECT_EQ(color.a, 200);

  ASSERT_TRUE(parseHex(L"#FC0", color));
  EXPECT_EQ(color.r, 255);
  EXPECT_EQ(color.g, 204);
  EXPECT_EQ(color.b, 0);
  EXPECT_EQ(color.a, 200);
}

TEST(ColorConvertTest, ParseHexRejectsInvalidText)
{
  ColorBgra color{};
  EXPECT_FALSE(parseHex(nullptr, color));
  EXPECT_FALSE(parseHex(L"", color));
  EXPECT_FALSE(parseHex(L"e3c363", color));
  EXPECT_FALSE(parseHex(L"#gg0000", color));
  EXPECT_FALSE(parseHex(L"#12345", color));
  EXPECT_FALSE(parseHex(L"#1234567", color));
}

TEST(ColorConvertTest, ParseChannelPercentHueClampAndReject)
{
  int value = -1;
  EXPECT_TRUE(parseChannel255(L"0", value));
  EXPECT_EQ(value, 0);
  EXPECT_TRUE(parseChannel255(L"300", value));
  EXPECT_EQ(value, 255);
  EXPECT_FALSE(parseChannel255(L"", value));
  EXPECT_FALSE(parseChannel255(L"12a", value));

  EXPECT_TRUE(parsePercent(L"100%", value));
  EXPECT_EQ(value, 100);
  EXPECT_TRUE(parsePercent(L"140", value));
  EXPECT_EQ(value, 100);
  EXPECT_FALSE(parsePercent(L"%", value));

  EXPECT_TRUE(parseHue(L"360", value));
  EXPECT_EQ(value, 360);
  EXPECT_TRUE(parseHue(L"-10", value));
  EXPECT_EQ(value, 0);
  EXPECT_FALSE(parseHue(L"h", value));
}

TEST(ColorConvertTest, RgbHsvRoundTripWithinOneLevel)
{
  const ColorBgra samples[] = {
      ColorBgra{0, 0, 220, 255},     ColorBgra{0, 140, 255, 180},
      ColorBgra{255, 255, 255, 255}, ColorBgra{0, 0, 0, 255},
      ColorBgra{99, 195, 227, 128},
  };
  for (const ColorBgra& sample : samples)
  {
    const ColorHsv hsv = rgbToHsv(sample);
    const ColorBgra back = hsvToRgb(hsv.hue_degrees, hsv.saturation, hsv.value,
                                    sample.a);
    EXPECT_EQ(back.a, sample.a);
    EXPECT_NEAR(back.r, sample.r, 1);
    EXPECT_NEAR(back.g, sample.g, 1);
    EXPECT_NEAR(back.b, sample.b, 1);
  }
}

TEST(ColorConvertTest, HsvPrimaryHues)
{
  const ColorBgra red = hsvToRgb(0, 100, 100, 255);
  EXPECT_EQ(red.r, 255);
  EXPECT_EQ(red.g, 0);
  EXPECT_EQ(red.b, 0);

  const ColorBgra green = hsvToRgb(120, 100, 100, 255);
  EXPECT_EQ(green.r, 0);
  EXPECT_EQ(green.g, 255);
  EXPECT_EQ(green.b, 0);
}

TEST(ColorConvertTest, BlendSrcOverKeepsOpaqueAndSkipsZeroAlpha)
{
  const std::uint32_t white = packColorBgra(ColorBgra{255, 255, 255, 255});
  const ColorBgra opaque_red{0, 0, 255, 255};
  EXPECT_EQ(blendSrcOver(white, opaque_red), packColorBgra(opaque_red));

  const ColorBgra invisible{0, 0, 255, 0};
  EXPECT_EQ(blendSrcOver(white, invisible), white);

  const ColorBgra half_red{0, 0, 255, 128};
  const std::uint32_t mixed = blendSrcOver(white, half_red);
  const ColorBgra out = unpackColorBgra(mixed);
  EXPECT_EQ(out.a, 255);
  EXPECT_EQ(out.r, 255);
  EXPECT_EQ(out.g, 127);
  EXPECT_EQ(out.b, 127);
}

TEST(ColorConvertTest, ColorsMatchRgbIgnoresAlpha)
{
  EXPECT_TRUE(colorsMatchRgb(ColorBgra{1, 2, 3, 10}, ColorBgra{1, 2, 3, 255}));
  EXPECT_FALSE(colorsMatchRgb(ColorBgra{1, 2, 3, 255}, ColorBgra{1, 2, 4, 255}));
}

}  // namespace
}  // namespace qingying
