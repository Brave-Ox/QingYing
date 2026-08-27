#include "qingying/export/dib_encoder.hpp"

#include <cstdint>
#include <cstring>
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
#pragma pack(pop)

}  // namespace

std::vector<std::uint8_t> encodeDib(const Image& image) {
  if (image.empty()) {
    return {};
  }
  const std::size_t num_pixels =
      static_cast<std::size_t>(image.width) *
      static_cast<std::size_t>(image.height);
  if (image.pixels.size() < num_pixels) {
    return {};  // 像素数据不足：防御性拒绝，避免越界读取。
  }

  const std::size_t row_bytes =
      static_cast<std::size_t>(image.width) * kBytesPerPixel;
  const std::size_t pixel_bytes =
      row_bytes * static_cast<std::size_t>(image.height);

  std::vector<std::uint8_t> out(kHeaderSize + pixel_bytes, 0);
  DibInfoHeader header{};
  header.biWidth = image.width;
  header.biHeight = image.height;  // 正 = 底向上（CF_DIB 标准）
  std::memcpy(out.data(), &header, sizeof(DibInfoHeader));

  // 像素区：底向上。DIB 第 i 行 = 原图第 (height-1-i) 行。
  std::uint8_t* const dst = out.data() + kHeaderSize;
  for (int src_row = 0; src_row < image.height; ++src_row) {
    const int dst_row = image.height - 1 - src_row;
    const std::size_t src_off =
        static_cast<std::size_t>(src_row) *
        static_cast<std::size_t>(image.width);
    const std::size_t dst_off = static_cast<std::size_t>(dst_row) * row_bytes;
    for (int col = 0; col < image.width; ++col) {
      const std::uint32_t px =
          image.pixels.at(src_off + static_cast<std::size_t>(col));
      const std::size_t out_off =
          dst_off + static_cast<std::size_t>(col) * kBytesPerPixel;
      dst[out_off + 0] = static_cast<std::uint8_t>(px & 0xFF);           // B
      dst[out_off + 1] = static_cast<std::uint8_t>((px >> 8) & 0xFF);    // G
      dst[out_off + 2] = static_cast<std::uint8_t>((px >> 16) & 0xFF);   // R
      dst[out_off + 3] = static_cast<std::uint8_t>((px >> 24) & 0xFF);   // A
    }
  }
  return out;
}

}  // namespace dib
}  // namespace qingying
