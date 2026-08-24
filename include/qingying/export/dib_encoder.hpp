#pragma once

#include <cstdint>
#include <vector>

#include "qingying/action/image.hpp"

namespace qingying {
namespace dib {

// 把 Image（BGRA32、行优先、顶向下）编码为剪贴板 CF_DIB 字节流：
// BITMAPINFOHEADER + 像素数据（底向上，biHeight 为正，标准 DIB 格式）。
// 纯计算，无 Win32 依赖，可单测。空图返回空向量。
std::vector<std::uint8_t> encodeDib(const Image& image);

}  // namespace dib
}  // namespace qingying
