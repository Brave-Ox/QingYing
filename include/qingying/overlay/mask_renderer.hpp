#pragma once

#include <cstdint>
#include <vector>

#include "qingying/overlay/selection_overlay.hpp"

namespace qingying {
namespace mask {

// 生成全屏遮罩的 32bpp BGRA 像素（premultiplied alpha，供分层窗口
// UpdateLayeredWindow 使用）：遮罩区域为半透明黑，选区内部完全透明
// （露出桌面），选区边框为不透明亮色。纯计算，无 Win32 依赖，可单测。
//
// 像素布局：高 8 位 alpha，低 24 位 BGR（premultiplied）。
// cancelled 或空选区时仅输出纯遮罩，不挖空。
void renderFullscreenMask(int width, int height,
                          const SelectionResult& selection,
                          std::vector<std::uint32_t>& out_pixels);

}  // namespace mask
}  // namespace qingying
