#include "browser_longshot_resolver.h"

#include <Windows.h>

#include <cstdint>
#include <cwchar>

namespace qingying::longshot_detail {

namespace {

constexpr int kWindowClassCapacity = 128;
constexpr int kMinimumContentExtentPx = 200;
constexpr wchar_t kChromiumRootClass[] = L"Chrome_WidgetWin_1";
constexpr wchar_t kChromiumRendererClass[] = L"Chrome_RenderWidgetHostHWND";
constexpr wchar_t kChromiumD3DSurfaceClass[] = L"Intermediate D3D Window";

bool getClassName(HWND window, wchar_t* class_name, int capacity) {
  if (window == nullptr || class_name == nullptr || capacity <= 1) {
    return false;
  }
  return GetClassNameW(window, class_name, capacity) > 0;
}

bool classNameEquals(HWND window, const wchar_t* expected) {
  wchar_t class_name[kWindowClassCapacity] = {};
  return expected != nullptr && getClassName(window, class_name,
                                              kWindowClassCapacity) &&
         _wcsicmp(class_name, expected) == 0;
}

bool getClientScreenRect(HWND window, RECT& out) {
  RECT client{};
  if (window == nullptr || !GetClientRect(window, &client)) {
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

bool rectContainsSelection(const RECT& rect, const LongShotRequest& request) {
  const LongShotProfileResult result{
      reinterpret_cast<std::uintptr_t>(GetDesktopWindow()), rect.left,
      rect.top, rect.right - rect.left, rect.bottom - rect.top};
  return result.containsSelection(request.x, request.y, request.width,
                                  request.height);
}

bool ownerIsUsableRoot(HWND owner) {
  return owner != nullptr && IsWindow(owner) && IsWindowVisible(owner) &&
         !IsIconic(owner) && GetAncestor(owner, GA_ROOT) == owner;
}

struct ContentCandidate {
  HWND window{nullptr};
  RECT screen_rect{};
  std::int64_t area{0};
  const wchar_t* expected_class{nullptr};
};

BOOL CALLBACK findLargestContentWindow(HWND window, LPARAM parameter) {
  auto* candidate = reinterpret_cast<ContentCandidate*>(parameter);
  if (candidate == nullptr || !IsWindowVisible(window) ||
      !classNameEquals(window, candidate->expected_class)) {
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
  if (width < kMinimumContentExtentPx || height < kMinimumContentExtentPx) {
    return TRUE;
  }

  const std::int64_t area = width * height;
  if (area > candidate->area) {
    candidate->window = window;
    candidate->screen_rect = screen_rect;
    candidate->area = area;
  }
  return TRUE;
}

bool resolveChromiumTarget(HWND owner, const LongShotRequest& request,
                           LongShotProfileResult& out) {
  ContentCandidate candidate;
  candidate.expected_class = kChromiumRendererClass;
  EnumChildWindows(owner, findLargestContentWindow,
                   reinterpret_cast<LPARAM>(&candidate));
  if (candidate.window == nullptr) {
    candidate.expected_class = kChromiumD3DSurfaceClass;
    EnumChildWindows(owner, findLargestContentWindow,
                     reinterpret_cast<LPARAM>(&candidate));
  }
  if (candidate.window == nullptr ||
      !rectContainsSelection(candidate.screen_rect, request)) {
    return false;
  }

  out.scroll_target = reinterpret_cast<std::uintptr_t>(candidate.window);
  out.content = ScreenPhysicalRect{
      candidate.screen_rect.left,
      candidate.screen_rect.top,
      candidate.screen_rect.right - candidate.screen_rect.left,
      candidate.screen_rect.bottom - candidate.screen_rect.top};
  return out.valid();
}

}  // namespace

bool resolveBrowserTarget(std::uintptr_t owner_window,
                          const LongShotRequest& request,
                          LongShotProfileResult& out) {
  out = LongShotProfileResult{};
  const HWND owner = reinterpret_cast<HWND>(owner_window);
  if (!request.valid() || request.owner_window != owner_window ||
      !ownerIsUsableRoot(owner) ||
      !classNameEquals(owner, kChromiumRootClass)) {
    return false;
  }
  return resolveChromiumTarget(owner, request, out);
}

}  // namespace qingying::longshot_detail
