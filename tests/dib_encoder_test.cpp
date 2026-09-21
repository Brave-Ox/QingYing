// BITMAPINFOHEADER / BI_RGB 用于断言 DIB 字节布局（测试层面引用 Win32 常量，
// encodeDib 本身是纯函数，不依赖 Win32）。
#include <Windows.h>

#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "qingying/export/dib_encoder.hpp"

namespace qingying {
namespace dib {

namespace {

// 构造单个像素的 BGRA 值（Image::pixels 的通道序：高 8 位 alpha，低 24 位 BGR）。
std::uint32_t kMakePixel(std::uint8_t b, std::uint8_t g, std::uint8_t r,
                         std::uint8_t a) {
  return (static_cast<std::uint32_t>(a) << 24) |
         (static_cast<std::uint32_t>(r) << 16) |
         (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
}

}  // namespace

TEST(DibEncoderTest, EmptyImageYieldsEmptyBytes) {
  const Image image;
  const auto bytes = encodeDib(image);
  EXPECT_TRUE(bytes.empty());
}

TEST(DibEncoderTest, HeaderIsBottomUpBgra32) {
  Image image;
  image.width = 2;
  image.height = 1;
  image.pixels = {kMakePixel(0, 0, 0, 0xFF), kMakePixel(0, 0, 0, 0xFF)};

  const auto bytes = encodeDib(image);
  ASSERT_FALSE(bytes.empty());
  ASSERT_GE(bytes.size(), sizeof(BITMAPINFOHEADER));

  // BITMAPINFOHEADER：biSize 40，biWidth/biHeight，biPlanes 1，biBitCount 32。
  const auto* header =
      reinterpret_cast<const BITMAPINFOHEADER*>(bytes.data());
  EXPECT_EQ(header->biSize, static_cast<std::uint32_t>(sizeof(BITMAPINFOHEADER)));
  EXPECT_EQ(header->biWidth, 2);
  EXPECT_EQ(header->biHeight, 1);   // 正 = 底向上
  EXPECT_EQ(header->biPlanes, 1);
  EXPECT_EQ(header->biBitCount, 32);
  EXPECT_EQ(header->biCompression, static_cast<std::uint32_t>(BI_RGB));
}

TEST(DibEncoderTest, PixelDataIsBottomUp) {
  // 2 行图：顶行红、底行蓝。CF_DIB 底向上 → 首像素应是原图的底行（蓝）。
  Image image;
  image.width = 2;
  image.height = 2;
  image.pixels = {
      kMakePixel(0xFF, 0x00, 0x00, 0xFF),  // 顶行：B=FF（红）
      kMakePixel(0x00, 0xFF, 0x00, 0xFF),  // 顶行：G=FF（绿）
      kMakePixel(0x00, 0x00, 0xFF, 0xFF),  // 底行：R=FF（蓝）
      kMakePixel(0x00, 0x00, 0x00, 0xFF),  // 底行：黑
  };

  const auto bytes = encodeDib(image);
  ASSERT_GE(bytes.size(),
            sizeof(BITMAPINFOHEADER) + 2u * 2u * 4u);
  const auto* px =
      reinterpret_cast<const std::uint32_t*>(bytes.data() +
                                             sizeof(BITMAPINFOHEADER));
  // 底向上：DIB 第一行 = 原图最后一行。
  EXPECT_EQ(px[0], kMakePixel(0x00, 0x00, 0xFF, 0xFF));  // 原底行：蓝
  EXPECT_EQ(px[1], kMakePixel(0x00, 0x00, 0x00, 0xFF));  // 原底行：黑
  EXPECT_EQ(px[2], kMakePixel(0xFF, 0x00, 0x00, 0xFF));  // 原顶行：红
  EXPECT_EQ(px[3], kMakePixel(0x00, 0xFF, 0x00, 0xFF));  // 原顶行：绿
}

TEST(DibEncoderTest, V5HeaderKeepsBgraMasksAndAlpha) {
  Image image;
  image.width = 1;
  image.height = 2;
  image.pixels = {
      kMakePixel(0x11, 0x22, 0x33, 0x44),
      kMakePixel(0x55, 0x66, 0x77, 0x88),
  };

  const auto bytes = encodeDibV5(image);
  ASSERT_GE(bytes.size(), sizeof(BITMAPV5HEADER) + 8u);
  const auto* header =
      reinterpret_cast<const BITMAPV5HEADER*>(bytes.data());
  EXPECT_EQ(header->bV5Size, sizeof(BITMAPV5HEADER));
  EXPECT_EQ(header->bV5Width, 1);
  EXPECT_EQ(header->bV5Height, 2);
  EXPECT_EQ(header->bV5Compression, static_cast<DWORD>(BI_BITFIELDS));
  EXPECT_EQ(header->bV5RedMask, 0x00FF0000u);
  EXPECT_EQ(header->bV5GreenMask, 0x0000FF00u);
  EXPECT_EQ(header->bV5BlueMask, 0x000000FFu);
  EXPECT_EQ(header->bV5AlphaMask, 0xFF000000u);

  const auto* pixels = reinterpret_cast<const std::uint32_t*>(
      bytes.data() + sizeof(BITMAPV5HEADER));
  EXPECT_EQ(pixels[0], kMakePixel(0x55, 0x66, 0x77, 0x88));
  EXPECT_EQ(pixels[1], kMakePixel(0x11, 0x22, 0x33, 0x44));
}

TEST(DibEncoderTest, RejectsImageWhoseDibByteCountWouldOverflow) {
  Image image;
  image.width = (std::numeric_limits<int>::max)();
  image.height = (std::numeric_limits<int>::max)();
  image.pixels = {0xFF000000u};

  EXPECT_TRUE(encodeDib(image).empty());
  EXPECT_TRUE(encodeDibV5(image).empty());
}

}  // namespace dib
}  // namespace qingying
