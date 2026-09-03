#pragma once

#include <cstdint>
#include <vector>

namespace qingying {

// 截图结果的统一载荷：一帧 BGRA32 位图，行优先，坐标按物理像素。
// capture 产出 → ResultStore 发布 → export / annotate / pin 消费。
// 刻意保持极薄：不携带算法/句柄；P0 以值传递（单区域，≤ 4K 屏）。
struct Image {
  int width{0};
  int height{0};
  std::vector<std::uint32_t> pixels;  // 每像素一个 uint32（BGRA 通道序）

  bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

}  // namespace qingying
