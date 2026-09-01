#include "qingying/longshot/explorer_longshot_profile.hpp"

#include "win32_scroll_helpers.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <vector>

namespace qingying {

namespace {

constexpr int kWindowClassCapacity = 128;
constexpr int kMinimumSelectionCoveragePercent = 80;

bool getClassName(HWND window, wchar_t* class_name, int capacity) {
  if (window == nullptr || class_name == nullptr || capacity <= 0) {
    return false;
  }
  class_name[0] = L'\0';
  return GetClassNameW(window, class_name, capacity) > 0;
}

bool classNameEquals(HWND window, const wchar_t* expected) {
  wchar_t class_name[kWindowClassCapacity] = {};
  return getClassName(window, class_name, kWindowClassCapacity) &&
         _wcsicmp(class_name, expected) == 0;
}

bool executableIsExplorer(HWND window) {
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id == 0) {
    return false;
  }

  const HANDLE process =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
  if (process == nullptr) {
    return false;
  }

  std::vector<wchar_t> path(32768, L'\0');
  DWORD path_size = static_cast<DWORD>(path.size());
  const BOOL query_ok =
      QueryFullProcessImageNameW(process, 0, path.data(), &path_size);
  CloseHandle(process);
  if (!query_ok || path_size == 0) {
    return false;
  }

  const wchar_t* base_name = path.data();
  for (DWORD i = 0; i < path_size; ++i) {
    if (path[i] == L'\\' || path[i] == L'/') {
      base_name = path.data() + i + 1;
    }
  }
  return _wcsicmp(base_name, L"explorer.exe") == 0;
}

bool isExplorerRoot(HWND window) {
  if (window == nullptr || GetAncestor(window, GA_ROOT) != window) {
    return false;
  }
  if (classNameEquals(window, L"CabinetWClass") ||
      classNameEquals(window, L"ExploreWClass")) {
    return true;
  }

  // 较新的 Explorer 版本可能将视图放在系统框架类窗口下。
  // 虽然这些 Shell 表面同样由 explorer.exe 所有，但仍要排除它们。
  if (classNameEquals(window, L"Progman") ||
      classNameEquals(window, L"WorkerW") ||
      classNameEquals(window, L"Shell_TrayWnd") ||
      classNameEquals(window, L"#32769")) {
    return false;
  }
  return executableIsExplorer(window);
}

bool isExplorerScrollTarget(HWND window) {
  // 这里包含经典 Shell 控件以及当前文件资源管理器使用的控件。
  // 加入 XAML/WinUI 宿主类名，是因为主文件视图可能挂在这些宿主下，
  // 而导航树仍然使用经典的 SysTreeView32。
  return classNameEquals(window, L"DirectUIHWND") ||
         classNameEquals(window, L"SysListView32") ||
         classNameEquals(window, L"SysTreeView32") ||
         classNameEquals(window, L"SHELLDLL_DefView") ||
         classNameEquals(window, L"DUIViewWndClassName") ||
         classNameEquals(window, L"UIItemsView") ||
         classNameEquals(window, L"ItemsView") ||
         classNameEquals(window, L"ListView") ||
         classNameEquals(window, L"ScrollViewer") ||
         classNameEquals(window, L"Windows.UI.Input.InputSite.WindowClass") ||
         classNameEquals(
             window, L"Windows.UI.Composition.DesktopWindowContentBridge") ||
         classNameEquals(window, L"Microsoft.UI.Content.DesktopChildSiteBridge");
}

bool hasVerticalScrollSurface(HWND window) {
  if (window == nullptr || !IsWindow(window)) {
    return false;
  }

  const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  if ((style & WS_VSCROLL) != 0) {
    return true;
  }

  // 自定义 Shell 视图可能提供滚动条，却没有在自身设置 WS_VSCROLL。
  // 这里只做只读探测，用于在普通 Explorer 界面元素之外识别未知的现代宿主。
  SCROLLBARINFO scrollbar{};
  scrollbar.cbSize = sizeof(scrollbar);
  return GetScrollBarInfo(window, OBJID_VSCROLL, &scrollbar) != FALSE &&
         (scrollbar.rgstate[0] & STATE_SYSTEM_INVISIBLE) == 0;
}

bool isExplorerScrollSurface(HWND window) {
  return isExplorerScrollTarget(window) || hasVerticalScrollSurface(window);
}

bool getClientScreenRect(HWND window, RECT& out) {
  RECT client{};
  if (!GetClientRect(window, &client)) {
    return false;
  }

  POINT top_left{client.left, client.top};
  POINT bottom_right{client.right, client.bottom};
  if (!ClientToScreen(window, &top_left) ||
      !ClientToScreen(window, &bottom_right)) {
    return false;
  }
  if (bottom_right.x <= top_left.x || bottom_right.y <= top_left.y) {
    return false;
  }

  out = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  return true;
}

bool containsSelection(const RECT& rect, const LongShotRequest& request) {
  const std::int64_t selection_left = request.x;
  const std::int64_t selection_top = request.y;
  const std::int64_t selection_right =
      selection_left + static_cast<std::int64_t>(request.width);
  const std::int64_t selection_bottom =
      selection_top + static_cast<std::int64_t>(request.height);
  return selection_left >= rect.left && selection_top >= rect.top &&
         selection_right <= rect.right && selection_bottom <= rect.bottom;
}

bool containsSelectionCenter(const RECT& rect,
                             const LongShotRequest& request) {
  const std::int64_t center_x =
      static_cast<std::int64_t>(request.x) + request.width / 2;
  const std::int64_t center_y =
      static_cast<std::int64_t>(request.y) + request.height / 2;
  return center_x >= rect.left && center_x < rect.right &&
         center_y >= rect.top && center_y < rect.bottom;
}

std::int64_t selectionArea(const LongShotRequest& request) {
  return static_cast<std::int64_t>(request.width) *
         static_cast<std::int64_t>(request.height);
}

std::int64_t intersectionArea(const RECT& rect,
                              const LongShotRequest& request) {
  const std::int64_t left =
      (std::max)(static_cast<std::int64_t>(rect.left),
                 static_cast<std::int64_t>(request.x));
  const std::int64_t top =
      (std::max)(static_cast<std::int64_t>(rect.top),
                 static_cast<std::int64_t>(request.y));
  const std::int64_t right =
      (std::min)(static_cast<std::int64_t>(rect.right),
                 static_cast<std::int64_t>(request.x) + request.width);
  const std::int64_t bottom =
      (std::min)(static_cast<std::int64_t>(rect.bottom),
                 static_cast<std::int64_t>(request.y) + request.height);
  if (right <= left || bottom <= top) {
    return 0;
  }
  return (right - left) * (bottom - top);
}

bool mostlyContainsSelection(const RECT& rect,
                             const LongShotRequest& request) {
  const std::int64_t requested_area = selectionArea(request);
  if (requested_area <= 0) {
    return false;
  }

  const std::int64_t overlap = intersectionArea(rect, request);
  // 分段计算允许超出区域的面积，确保对 int 范围内的最大矩形比较时不会溢出。
  constexpr int kMaximumSelectionOutsidePercent =
      100 - kMinimumSelectionCoveragePercent;
  const std::int64_t maximum_outside =
      (requested_area / 100) * kMaximumSelectionOutsidePercent +
      (requested_area % 100) * kMaximumSelectionOutsidePercent / 100;
  const std::int64_t minimum_overlap = requested_area - maximum_outside;
  return overlap >= minimum_overlap;
}

struct ExplorerCandidate {
  const LongShotRequest* request{nullptr};
  HWND window{nullptr};
  RECT screen_rect{};
  std::int64_t area{0};
  std::int64_t overlap_area{0};
  bool fully_contains_selection{false};
  bool hit_path{false};
};

void considerExplorerCandidate(HWND window, ExplorerCandidate& candidate,
                               bool from_hit_path) {
  if (candidate.request == nullptr || !IsWindowVisible(window) ||
      !isExplorerScrollSurface(window)) {
    return;
  }
  RECT screen_rect{};
  if (!getClientScreenRect(window, screen_rect)) {
    return;
  }

  const std::int64_t overlap_area =
      intersectionArea(screen_rect, *candidate.request);
  if (overlap_area <= 0 ||
      !mostlyContainsSelection(screen_rect, *candidate.request)) {
    return;
  }

  // 对现代 Explorer 来说，命中测试得到的目标最可靠：这样可以避免文件列表选区
  // 因为相邻导航窗格的矩形更大而被错误解析到导航窗格。
  if (from_hit_path &&
      !containsSelectionCenter(screen_rect, *candidate.request)) {
    return;
  }

  // 优先选择完整包含选区的控件。即使选区包含少量列表表头或滚动条边缘，
  // 上面的覆盖率检查仍会让实际内容控件胜出，同时拒绝跨越多个窗格的选区。
  const bool fully_contains =
      containsSelection(screen_rect, *candidate.request);
  const std::int64_t width =
      static_cast<std::int64_t>(screen_rect.right) - screen_rect.left;
  const std::int64_t height =
      static_cast<std::int64_t>(screen_rect.bottom) - screen_rect.top;
  const std::int64_t area = width * height;
  const bool better_candidate =
      candidate.window == nullptr ||
      (from_hit_path && !candidate.hit_path) ||
      (from_hit_path == candidate.hit_path &&
       ((fully_contains && !candidate.fully_contains_selection) ||
        (fully_contains == candidate.fully_contains_selection &&
         (overlap_area > candidate.overlap_area ||
          (overlap_area == candidate.overlap_area && area < candidate.area)))));
  if (better_candidate) {
    candidate.window = window;
    candidate.screen_rect = screen_rect;
    candidate.area = area;
    candidate.overlap_area = overlap_area;
    candidate.fully_contains_selection = fully_contains;
    candidate.hit_path = from_hit_path;
  }
}

BOOL CALLBACK findSmallestContainingTarget(HWND window, LPARAM parameter) {
  auto* candidate = reinterpret_cast<ExplorerCandidate*>(parameter);
  if (candidate != nullptr) {
    considerExplorerCandidate(window, *candidate, false);
  }
  return TRUE;
}

void considerHitPath(HWND owner, ExplorerCandidate& candidate) {
  const LongShotRequest& request = *candidate.request;
  const std::int64_t center_x =
      static_cast<std::int64_t>(request.x) + request.width / 2;
  const std::int64_t center_y =
      static_cast<std::int64_t>(request.y) + request.height / 2;
  if (center_x < (std::numeric_limits<LONG>::min)() ||
      center_x > (std::numeric_limits<LONG>::max)() ||
      center_y < (std::numeric_limits<LONG>::min)() ||
      center_y > (std::numeric_limits<LONG>::max)()) {
    return;
  }

  const POINT screen_point{static_cast<LONG>(center_x),
                           static_cast<LONG>(center_y)};
  HWND current = owner;
  for (;;) {
    POINT client_point = screen_point;
    if (!ScreenToClient(current, &client_point)) {
      return;
    }
    const HWND child = ChildWindowFromPointEx(
        current, client_point,
        CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
    if (child == nullptr || child == current) {
      break;
    }
    current = child;
  }

  // 从最深层的命中子窗口向上遍历，即使命中点落在表头或项目辅助窗口上，
  // 也能选中它的可滚动祖先控件。
  while (current != nullptr && current != owner) {
    considerExplorerCandidate(current, candidate, true);
    current = GetParent(current);
  }
}

}  // 匿名命名空间

bool resolveExplorerProfile(const LongShotRequest& request,
                            LongShotProfileResult& out) {
  out = LongShotProfileResult{};
  HWND owner = reinterpret_cast<HWND>(request.owner_window);
  if (owner != nullptr) {
    const HWND root = GetAncestor(owner, GA_ROOT);
    if (root != nullptr) {
      owner = root;
    }
  }
  if (!request.valid() || owner == nullptr || !IsWindow(owner) ||
      !IsWindowVisible(owner) || IsIconic(owner) || !isExplorerRoot(owner)) {
    return false;
  }

  ExplorerCandidate candidate;
  candidate.request = &request;
  // 先解析选区中心实际命中的子控件。这对 Windows 11 Explorer 很重要，
  // 因为文件列表和导航树可能是类名相近的同级 DirectUI 宿主。
  considerHitPath(owner, candidate);
  EnumChildWindows(owner, findSmallestContainingTarget,
                   reinterpret_cast<LPARAM>(&candidate));
  if (candidate.window == nullptr) {
    return false;
  }

  out.scroll_target = reinterpret_cast<std::uintptr_t>(candidate.window);
  out.content_x = candidate.screen_rect.left;
  out.content_y = candidate.screen_rect.top;
  out.content_width = candidate.screen_rect.right - candidate.screen_rect.left;
  out.content_height =
      candidate.screen_rect.bottom - candidate.screen_rect.top;
  return out.valid();
}

bool ExplorerLongShotProfile::resolve(const LongShotRequest& request,
                                      LongShotProfileResult& out) const {
  return resolveExplorerProfile(request, out);
}

bool ExplorerLongShotProfile::scrollDown(
    const LongShotRequest& request,
    const LongShotProfileResult& profile) const {
  return longshot_detail::sendWheelDown(request, profile);
}

bool ExplorerLongShotProfile::queryScrollState(
    const LongShotProfileResult& profile, LongShotScrollState& out) const {
  return longshot_detail::queryVerticalScrollState(profile, out);
}

}  // qingying 命名空间
