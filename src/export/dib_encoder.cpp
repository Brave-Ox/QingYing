#include "qingying/export/dib_encoder.hpp"

#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace qingying {
namespace dib {

namespace {

constexpr int kBytesPerPixel = 4;
// BITMAPINFOHEADER 固定 40 字节：按标准布局直接编码，无需依赖 Win32 头。
constexpr std::uint32_t kHeaderSize = 40u;
constexpr std::uint16_t kPlanes = 1;
constexpr std::uint16_t kBitCount = 32;
constexpr std::uint32_t kCompressionRgb = 0u;  // BI_RGB = 0
constexpr std::uint32_t kCompressionBitFields = 3u;  // BI_BITFIELDS = 3
constexpr std::uint32_t kV5HeaderSize = 124u;
constexpr std::uint32_t kLcsSrgb = 0x73524742u;  // 'sRGB'
constexpr std::uint32_t kLcsGmImages = 4u;

// 与 Windows BITMAPINFOHEADER 完全同布局（pack 1，40 字节）。
#pragma pack(push, 1)
struct DibInfoHeader {
  std::uint32_t biSize{kHeaderSize};
  std::int32_t biWidth{0};
  std::int32_t biHeight{0};
  std::uint16_t biPlanes{kPlanes};
  std::uint16_t biBitCount{kBitCount};
  std::uint32_t biCompression{kCompressionRgb};
  std::uint32_t biSizeImage{0};
  std::int32_t biXPelsPerMeter{0};
  std::int32_t biYPelsPerMeter{0};
  std::uint32_t biClrUsed{0};
  std::uint32_t biClrImportant{0};
};

// 与 Windows BITMAPV5HEADER 完全同布局（pack 1，124 字节）。
struct DibV5Header {
  std::uint32_t bV5Size{kV5HeaderSize};
  std::int32_t bV5Width{0};
  std::int32_t bV5Height{0};
  std::uint16_t bV5Planes{kPlanes};
  std::uint16_t bV5BitCount{kBitCount};
  std::uint32_t bV5Compression{kCompressionBitFields};
  std::uint32_t bV5SizeImage{0};
  std::int32_t bV5XPelsPerMeter{0};
  std::int32_t bV5YPelsPerMeter{0};
  std::uint32_t bV5ClrUsed{0};
  std::uint32_t bV5ClrImportant{0};
  std::uint32_t bV5RedMask{0x00FF0000u};
  std::uint32_t bV5GreenMask{0x0000FF00u};
  std::uint32_t bV5BlueMask{0x000000FFu};
  std::uint32_t bV5AlphaMask{0xFF000000u};
  std::uint32_t bV5CSType{kLcsSrgb};
  std::uint8_t bV5Endpoints[36]{};
  std::uint32_t bV5GammaRed{0};
  std::uint32_t bV5GammaGreen{0};
  std::uint32_t bV5GammaBlue{0};
  std::uint32_t bV5Intent{kLcsGmImages};
  std::uint32_t bV5ProfileData{0};
  std::uint32_t bV5ProfileSize{0};
  std::uint32_t bV5Reserved{0};
};
#pragma pack(pop)

static_assert(sizeof(DibInfoHeader) == kHeaderSize);
static_assert(sizeof(DibV5Header) == kV5HeaderSize);

bool imageLayout(const Image& image, std::size_t* row_bytes,
                 std::size_t* pixel_bytes) {
  if (image.empty()) return false;

  const auto width = static_cast<std::size_t>(image.width);
  const auto height = static_cast<std::size_t>(image.height);
  if (width > (std::numeric_limits<std::size_t>::max)() / kBytesPerPixel) {
    return false;
  }
  const std::size_t row = width * kBytesPerPixel;
  if (height > (std::numeric_limits<std::size_t>::max)() / row) {
    return false;
  }
  const std::size_t pixels = row * height;
  if (pixels > (std::numeric_limits<std::uint32_t>::max)() ||
      image.pixels.size() < width * height) {
    return false;
  }
  *row_bytes = row;
  *pixel_bytes = pixels;
  return true;
}

void copyBottomUpPixels(const Image& image, std::uint8_t* destination,
                        std::size_t row_bytes) {
  for (int src_row = 0; src_row < image.height; ++src_row) {
    const int dst_row = image.height - 1 - src_row;
    const std::size_t src_off =
        static_cast<std::size_t>(src_row) * static_cast<std::size_t>(image.width);
    const std::size_t dst_off = static_cast<std::size_t>(dst_row) * row_bytes;
    for (int col = 0; col < image.width; ++col) {
      const std::uint32_t px =
          image.pixels.at(src_off + static_cast<std::size_t>(col));
      const std::size_t out_off =
          dst_off + static_cast<std::size_t>(col) * kBytesPerPixel;
      destination[out_off + 0] = static_cast<std::uint8_t>(px & 0xFF);
      destination[out_off + 1] = static_cast<std::uint8_t>((px >> 8) & 0xFF);
      destination[out_off + 2] = static_cast<std::uint8_t>((px >> 16) & 0xFF);
      destination[out_off + 3] = static_cast<std::uint8_t>((px >> 24) & 0xFF);
    }
  }
}

}  // namespace

DibBytes encodeDib(const Image& image) {
  std::size_t row_bytes = 0;
  std::size_t pixel_bytes = 0;
  if (!imageLayout(image, &row_bytes, &pixel_bytes)) return {};

  DibBytes out(kHeaderSize + pixel_bytes);
  DibInfoHeader header{};
  header.biWidth = image.width;
  header.biHeight = image.height;  // 正 = 底向上（CF_DIB 标准）
  std::memcpy(out.data(), &header, sizeof(DibInfoHeader));

  // 像素区：底向上。DIB 第 i 行 = 原图第 (height-1-i) 行。
  copyBottomUpPixels(image, out.data() + kHeaderSize, row_bytes);
  return out;
}

DibBytes encodeDibV5(const Image& image) {
  std::size_t row_bytes = 0;
  std::size_t pixel_bytes = 0;
  if (!imageLayout(image, &row_bytes, &pixel_bytes)) return {};

  DibBytes out(kV5HeaderSize + pixel_bytes);
  DibV5Header header{};
  header.bV5Width = image.width;
  header.bV5Height = image.height;
  header.bV5SizeImage = static_cast<std::uint32_t>(pixel_bytes);
  std::memcpy(out.data(), &header, sizeof(DibV5Header));
  copyBottomUpPixels(image, out.data() + kV5HeaderSize, row_bytes);
  return out;
}

}  // namespace dib
}  // namespace qingying
