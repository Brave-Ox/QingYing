#pragma once

#include <cstdint>
#include <vector>

#include "qingying/action/image.hpp"
#include "qingying/geometry/rect_types.h"

namespace qingying {
namespace mask {

// 生成全屏遮罩的 32bpp BGRA 像素（premultiplied alpha，供分层窗口
// UpdateLayeredWindow 使用）：遮罩区域为半透明黑，选区内部完全透明
// （露出桌面），选区边框为不透明亮色。纯计算，无 Win32 依赖，可单测。
//
// 像素布局：高 8 位 alpha，低 24 位 BGR（premultiplied）。
// cancelled 或空选区时仅输出纯遮罩，不挖空。
void renderFullscreenMask(int width, int height,
                          const OverlayClientRect& selection,
                          std::vector<std::uint32_t>& out_pixels);

// 把不透明截图背景（Image，BGRA32）与遮罩像素合成，得到遮罩界面最终帧：
//   out = background * (1 - mask_alpha/255) + mask（mask RGB 为 premultiplied）
// 输出不透明（alpha 恒 0xFF）BGRA32，尺寸与 background 一致。
// background 为空或 mask 尺寸不一致时返回 false，out 保持不变。
bool composeBackground(const Image& background,
                       const std::vector<std::uint32_t>& mask_pixels,
                       std::vector<std::uint32_t>& out_pixels);

}  // namespace mask
}  // namespace qingying
