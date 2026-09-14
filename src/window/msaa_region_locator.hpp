#pragma once

#include <cstddef>
#include <cstdint>

#include <Windows.h>
#include <oleacc.h>

#include "qingying/window/smart_region_detector.hpp"

namespace qingying::window_detail {

struct MsaaRegionProperties
{
  WindowRect rect;
  LONG role{0};
  LONG state{0};
};

// Chromium 工具栏与 Pane 往往仅在直接命中时返回容器；这些角色允许在
// 浏览器后台查询中继续枚举包含鼠标的子节点。
bool msaaShouldEnumerateChildren(
    const MsaaRegionProperties& properties) noexcept;

// 浏览器直接命中工具栏等泛化容器时，先在既有预算内查找包含鼠标的
// 子元素；普通窗口仍可立即采用其直接命中结果。
bool msaaShouldDeferDirectBrowserContainerCandidate(
    bool is_browser_window,
    const MsaaRegionProperties& properties) noexcept;

// 枚举浏览器容器时，仅允许覆盖当前鼠标位置的子节点继续消耗下钻预算；
// 其余同级节点仍可被扫描，但不得进入递归分支。
bool msaaChildContainsScreenPoint(const WindowRect& child_rect,
                                  POINT screen_point) noexcept;

// 某些 Chromium 容器报告的 childCount 为 0，但仍可通过
// AccessibleChildren 枚举出虚拟子节点。返回值同时受单层和总预算约束。
LONG msaaAccessibleChildrenRequestCount(
    LONG reported_child_count, std::size_t remaining_budget) noexcept;

// 仅在 Chromium 顶部浏览器壳中，对语义或可见性过滤的命中节点恢复
// 父容器下钻；网页正文、普通窗口和其他过滤原因不得走该路径。
bool msaaShouldRecoverBrowserFilteredNode(
    bool is_browser_window, bool is_browser_top_chrome,
    SmartRegionMsaaFilteredNodeReason reason) noexcept;

// MSAA 的 accLocation 使用客户区屏幕坐标；用于拒绝覆盖整个根客户区的
// ContentSurface，避免浏览器窗口外壳进入异步候选。
bool msaaRectCoversRootClientArea(const WindowRect& rect,
                                  const WindowRect& root_client_rect) noexcept;

// Chromium 浏览器的可访问性树通常比普通桌面窗口多几层中间容器。
// 此函数将深度策略保持为可单测的纯逻辑，实际窗口类别由实现内部判定。
std::uint8_t msaaHitTestDepthLimit(bool is_browser_window) noexcept;

bool makeMsaaCandidate(HWND root_window, HWND target_window,
                       POINT screen_point,
                       const MsaaRegionProperties& properties,
                       SmartRegionCandidate& out,
                       std::uint8_t accessibility_depth = 0,
                       SmartRegionMsaaFilteredNodeDiagnostic*
                           out_filtered_node = nullptr) noexcept;

// 仅在 UIA 没有有效局部候选时调用；失败时保持 out 无效。
bool locateMsaaCandidate(HWND root_window, POINT screen_point,
                         SmartRegionCandidate& out,
                         SmartRegionMsaaTraversalDiagnostic*
                             out_diagnostic = nullptr,
                         bool* out_browser_semantic_miss = nullptr) noexcept;

}  // namespace qingying::window_detail
