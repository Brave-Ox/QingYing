#include "chrome_longshot_resolver.h"

#include <Windows.h>

#include <cstdint>
#include <cwchar>

namespace qingying::longshot_detail {

namespace {

constexpr int kWindowClassCapacity = 128;
constexpr wchar_t kChromeRootClass[] = L"Chrome_WidgetWin_1";
constexpr wchar_t kChromeRendererClass[] = L"Chrome_RenderWidgetHostHWND";
constexpr wchar_t kChromeD3DSurfaceClass[] = L"Intermediate D3D Window";
constexpr int kMinimumContentExtentPx = 200;

bool classNameEquals(HWND window, const wchar_t* expected) {
  wchar_t class_name[kWindowClassCapacity] = {};
  const int length =
      GetClassNameW(window, class_name, kWindowClassCapacity);
  return length > 0 && _wcsicmp(class_name, expected) == 0;
}

bool getClientScreenRect(HWND window, RECT& out) {
  RECT client{};
  if (!GetClientRect(window, &client)) {
    return false;
  }

  POINT top_left{client.left, client.top};
  POINT bottom_right{client.right, client.bottom};
  if (!ClientToScreen(window, &top_left) ||
      !ClientToScreen(window, &bottom_right) ||
      bottom_right.x <= top_left.x || bottom_right.y <= top_left.y) {
    return false;
  }

  out = {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  return true;
}

struct RendererCandidate {
  HWND window{nullptr};
  RECT screen_rect{};
  std::int64_t area{0};
};

BOOL CALLBACK findLargestRenderer(HWND window, LPARAM parameter) {
  auto* candidate = reinterpret_cast<RendererCandidate*>(parameter);
  if (!classNameEquals(window, kChromeRendererClass)) {
    return TRUE;
  }

  RECT screen_rect{};
  if (!getClientScreenRect(window, screen_rect)) {
    return TRUE;
  }

  const std::int64_t width =
      static_cast<std::int64_t>(screen_rect.right) - screen_rect.left;
  const std::int64_t height =
      static_cast<std::int64_t>(screen_rect.bottom) - screen_rect.top;
  const std::int64_t area = width * height;
  // 新版 Chrome 的地址栏也可能使用这个窗口类，但高度远小于网页正文。
  if (width < kMinimumContentExtentPx || height < kMinimumContentExtentPx) {
    return TRUE;
  }
  if (area > candidate->area) {
    candidate->window = window;
    candidate->screen_rect = screen_rect;
    candidate->area = area;
  }
  return TRUE;
}

BOOL CALLBACK findLargestVisibleD3DSurface(HWND window, LPARAM parameter) {
  auto* candidate = reinterpret_cast<RendererCandidate*>(parameter);
  if (!IsWindowVisible(window) ||
      !classNameEquals(window, kChromeD3DSurfaceClass)) {
    return TRUE;
  }

  RECT screen_rect{};
  if (!getClientScreenRect(window, screen_rect)) {
    return TRUE;
  }

  const std::int64_t width =
      static_cast<std::int64_t>(screen_rect.right) - screen_rect.left;
  const std::int64_t height =
      static_cast<std::int64_t>(screen_rect.bottom) - screen_rect.top;
  const std::int64_t area = width * height;
  if (width < kMinimumContentExtentPx || height < kMinimumContentExtentPx) {
    return TRUE;
  }
  if (area > candidate->area) {
    candidate->window = window;
    candidate->screen_rect = screen_rect;
    candidate->area = area;
  }
  return TRUE;
}

}  // namespace

bool resolveChromeTarget(std::uintptr_t owner_window,
                         LongShotProfileResult& out) {
  out = LongShotProfileResult{};
  const HWND owner = reinterpret_cast<HWND>(owner_window);
  if (owner == nullptr || !IsWindow(owner) || !IsWindowVisible(owner) ||
      IsIconic(owner) || GetAncestor(owner, GA_ROOT) != owner ||
      !classNameEquals(owner, kChromeRootClass)) {
    return false;
  }

  RendererCandidate candidate;
  EnumChildWindows(owner, findLargestRenderer,
                   reinterpret_cast<LPARAM>(&candidate));
  if (candidate.window == nullptr) {
    // Chrome 120+ 在部分 GPU/合成模式下不会为网页正文保留可见的
    // Chrome_RenderWidgetHostHWND，而是把输入与绘制落在 D3D 合成表面。
    // 此时依然由用户框选正文，插件只把该表面作为滚轮目标。
    EnumChildWindows(owner, findLargestVisibleD3DSurface,
                     reinterpret_cast<LPARAM>(&candidate));
  }
  if (candidate.window == nullptr) {
    return false;
  }

  out.scroll_target = reinterpret_cast<std::uintptr_t>(candidate.window);
  out.content_x = static_cast<int>(candidate.screen_rect.left);
  out.content_y = static_cast<int>(candidate.screen_rect.top);
  out.content_width =
      static_cast<int>(candidate.screen_rect.right - candidate.screen_rect.left);
  out.content_height = static_cast<int>(candidate.screen_rect.bottom -
                                        candidate.screen_rect.top);
  if (!out.valid()) {
    out = LongShotProfileResult{};
    return false;
  }
  return true;
}

}  // namespace qingying::longshot_detail
