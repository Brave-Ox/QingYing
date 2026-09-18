#pragma once

#include <cstdint>
#include <algorithm>
#include "qingying/action/image_memory_budget.hpp"
#include <vector>

namespace qingying {

class ImagePixels : public std::vector<std::uint32_t, ImageAllocator<std::uint32_t>> {
  using Base = std::vector<std::uint32_t, ImageAllocator<std::uint32_t>>;
 public:
  using Base::Base;
  using Base::operator=;
  ImagePixels() = default;
  ImagePixels(const std::vector<std::uint32_t>& values) : Base(values.begin(), values.end()) {}
  ImagePixels& operator=(const std::vector<std::uint32_t>& values) {
    assign(values.begin(), values.end()); return *this;
  }
  friend bool operator==(const ImagePixels& left, const std::vector<std::uint32_t>& right) {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin());
  }
  friend bool operator==(const std::vector<std::uint32_t>& left, const ImagePixels& right) { return right == left; }
};

// 截图结果的统一载荷：一帧 BGRA32 位图，行优先，坐标按物理像素。
// capture 产出 → ResultStore 发布 → export / annotate / pin 消费。
// 刻意保持极薄：不携带算法/句柄；P0 以值传递（单区域，≤ 4K 屏）。
struct Image {
  int width{0};
  int height{0};
  ImagePixels pixels;  // 分配前核算容量，扩容时同时计入旧/新缓冲峰值。

  bool classifyMemory(ImageMemoryKind kind) const noexcept {
    return ImageMemoryBudget::global().classify(pixels.data(), kind);
  }

  bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

}  // namespace qingying
