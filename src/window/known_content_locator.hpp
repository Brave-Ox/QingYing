#pragma once

#include <Windows.h>

#include "qingying/window/smart_region_detector.hpp"

namespace qingying::window_detail {

// 根据 Chromium 网页渲染区推导浏览器外壳顶部区域。只有鼠标实际位于
// 外壳区域时返回 true，网页渲染区始终保留给已有的内容定位路径。
bool chromiumBrowserChromeRect(const WindowRect& root_client_rect,
                               const WindowRect& renderer_rect,
                               POINT screen_point,
                               WindowRect& out) noexcept;

// Electron 工作台中的 Chromium Renderer 通常覆盖整个客户区；这种泛化
// 容器不能作为编辑器、终端或侧栏的 KnownContent 候选。
bool electronWorkbenchRendererCoversClientArea(
    const WindowRect& root_client_rect,
    const WindowRect& renderer_rect) noexcept;

bool locateKnownContent(HWND root_window, POINT screen_point,
                        SmartRegionCandidate& out,
                        WindowRect* chromium_browser_chrome = nullptr,
                        bool* use_workbench_visual_policy = nullptr) noexcept;

}  // namespace qingying::window_detail
