#pragma once

#include <cstdint>
#include <vector>

namespace qingying {

// 选区八点/四边调整手柄与整体移动的标识。
// 纯几何语义，不依赖 Win32；供 SelectionController（命中测试）与
// SelectionOverlay（手柄绘制）共用。
enum class SelectionHandle {
  None,        // 不在选区上
  Move,        // 选区内部（整体移动）
  TopLeft,     // 左上角
  Top,         // 上边中点
  TopRight,    // 右上角
  Right,       // 右边中点
  BottomRight, // 右下角
  Bottom,      // 下边中点
  BottomLeft,  // 左下角
  Left,        // 左边中点
};

namespace handles {

// 手柄命中半径（逻辑像素）：点与角/边距离在半径内即视为命中。
inline constexpr int kHandleHitRadius = 5;

// 命中测试：返回点 (x, y) 落在选区 (sx, sy, sw, sh) 上的哪个手柄 / 内部 / 外部。
// 坐标为覆盖层客户区坐标（与选区同坐标系）。角优先于边、边优先于内部。
SelectionHandle hitTest(int x, int y, int sx, int sy, int sw, int sh,
                        int radius = kHandleHitRadius);

// 在 BGRA32 像素缓冲上绘制 8 个实心方块手柄（四个角 + 四条边中点）。
// color 为手柄颜色（BGRA32，如不透明白 0xFFFFFFFF）。越界部分自动钳制跳过。
void drawHandles(std::vector<std::uint32_t>& pixels, int width, int height,
                 int sx, int sy, int sw, int sh, std::uint32_t color,
                 int radius = kHandleHitRadius);

}  // namespace handles
}  // namespace qingying
