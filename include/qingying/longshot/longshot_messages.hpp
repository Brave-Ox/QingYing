#pragma once

#include <Windows.h>

namespace qingying {

// 由 longshot 模块的外部结果接收端投递。具体来源不泄漏到 app。
constexpr UINT WM_QINGYING_LONGSHOT_EXTERNAL_RESULT = WM_APP + 5;

}  // namespace qingying
