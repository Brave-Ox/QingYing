#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

#include "qingying/action/image.hpp"

namespace qingying {
namespace dib {

// 把 Image（BGRA32、行优先、顶向下）编码为剪贴板 CF_DIB 字节流：
// BITMAPINFOHEADER + 像素数据（底向上，biHeight 为正，标准 DIB 格式）。
// 纯计算，无 Win32 依赖，可单测。空图返回空向量。
class DibBytes {
 public:
  DibBytes() = default;
  explicit DibBytes(std::size_t size)
      : m_budget(ImageMemoryBudget::global().reserve(
            size, ImageMemoryKind::EncodeScratch)) {
    if (!m_budget) throw std::bad_alloc{};
    m_bytes = std::make_unique<std::uint8_t[]>(size);
    m_size = size;
  }

  DibBytes(const DibBytes&) = delete;
  DibBytes& operator=(const DibBytes&) = delete;
  DibBytes(DibBytes&&) noexcept = default;
  DibBytes& operator=(DibBytes&&) noexcept = default;

  bool empty() const noexcept { return m_size == 0; }
  std::size_t size() const noexcept { return m_size; }
  std::uint8_t* data() noexcept { return m_bytes.get(); }
  const std::uint8_t* data() const noexcept { return m_bytes.get(); }

 private:
  ImageMemoryBudget::Token m_budget;
  std::unique_ptr<std::uint8_t[]> m_bytes;
  std::size_t m_size{0};
};
DibBytes encodeDib(const Image& image);

// CF_DIBV5 兼容格式：BITMAPV5HEADER + BGRA 像素数据。它显式声明通道掩码
// 与 Alpha，供 Chromium、现代 Office/WPS 等优先读取，同时仍由 CF_DIB 兜底。
// 输出同样是底向上像素顺序，空图或不安全的尺寸返回空向量。
DibBytes encodeDibV5(const Image& image);

}  // namespace dib
}  // namespace qingying
