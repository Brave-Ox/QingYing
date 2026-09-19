#include "win32_scroll_helpers.hpp"

#include <Windows.h>

#include <cstdint>
#include <limits>

namespace qingying {
namespace longshot_detail {

namespace {

constexpr UINT kScrollDispatchTimeoutMs = 500;
constexpr UINT kGenericScrollDispatchTimeoutMs = 200;

}  // 匿名命名空间

bool windowBelongsToOwner(std::uintptr_t owner_window,
                          std::uintptr_t target_window) noexcept {
  const HWND owner = reinterpret_cast<HWND>(owner_window);
  const HWND target = reinterpret_cast<HWND>(target_window);
  return owner != nullptr && target != nullptr &&
         (owner == target || IsChild(owner, target));
}

bool windowClientScreenRect(std::uintptr_t window,
                            ScreenPhysicalRect& out) noexcept {
  out = {};
  const HWND target = reinterpret_cast<HWND>(window);
  if (target == nullptr || !IsWindow(target)) return false;
  RECT client{};
  if (!GetClientRect(target, &client)) return false;
  POINT top_left{client.left, client.top};
  POINT bottom_right{client.right, client.bottom};
  if (!ClientToScreen(target, &top_left) ||
      !ClientToScreen(target, &bottom_right)) {
    return false;
  }
  const auto width = static_cast<std::int64_t>(bottom_right.x) - top_left.x;
  const auto height = static_cast<std::int64_t>(bottom_right.y) - top_left.y;
  if (width <= 0 || height <= 0 ||
      width > (std::numeric_limits<int>::max)() ||
      height > (std::numeric_limits<int>::max)()) {
    return false;
  }
  out = {top_left.x, top_left.y, static_cast<int>(width),
         static_cast<int>(height)};
  return true;
}

std::uint32_t windowProcessId(std::uintptr_t window) noexcept {
  DWORD process_id = 0;
  const HWND target = reinterpret_cast<HWND>(window);
  if (target != nullptr) GetWindowThreadProcessId(target, &process_id);
  return static_cast<std::uint32_t>(process_id);
}

bool sendBoundedWheelDown(std::uintptr_t owner_window,
                          std::uintptr_t target_window, int screen_x,
                          int screen_y) noexcept {
  if (!windowBelongsToOwner(owner_window, target_window)) return false;
  if (screen_x < (std::numeric_limits<SHORT>::min)() ||
      screen_x > (std::numeric_limits<SHORT>::max)() ||
      screen_y < (std::numeric_limits<SHORT>::min)() ||
      screen_y > (std::numeric_limits<SHORT>::max)()) {
    return false;
  }
  const HWND target = reinterpret_cast<HWND>(target_window);
  if (target == nullptr || !IsWindow(target)) return false;

  const WORD wheel_delta = static_cast<WORD>(static_cast<SHORT>(-WHEEL_DELTA));
  const WPARAM wheel_parameters = MAKEWPARAM(0, wheel_delta);
  const LPARAM screen_point =
      MAKELPARAM(static_cast<WORD>(screen_x), static_cast<WORD>(screen_y));
  DWORD_PTR message_result = 0;
  return SendMessageTimeoutW(
             target, WM_MOUSEWHEEL, wheel_parameters, screen_point,
             SMTO_ABORTIFHUNG | SMTO_BLOCK, kGenericScrollDispatchTimeoutMs,
             &message_result) != 0;
}

bool sendWheelDown(const LongShotRequest& request,
                   const LongShotProfileResult& profile) {
  const HWND scroll_target = reinterpret_cast<HWND>(profile.scroll_target);
  if (scroll_target == nullptr || !IsWindow(scroll_target)) {
    return false;
  }

  const std::int64_t center_x =
      static_cast<std::int64_t>(request.x) + request.width / 2;
  const std::int64_t center_y =
      static_cast<std::int64_t>(request.y) + request.height / 2;
  if (center_x < (std::numeric_limits<int>::min)() ||
      center_x > (std::numeric_limits<int>::max)() ||
      center_y < (std::numeric_limits<int>::min)() ||
      center_y > (std::numeric_limits<int>::max)()) {
    return false;
  }

  const WORD wheel_delta = static_cast<WORD>(static_cast<SHORT>(-WHEEL_DELTA));
  const WPARAM wheel_parameters = MAKEWPARAM(0, wheel_delta);
  const LPARAM screen_point =
      MAKELPARAM(static_cast<WORD>(static_cast<int>(center_x)),
                 static_cast<WORD>(static_cast<int>(center_y)));
  DWORD_PTR message_result = 0;
  if (SendMessageTimeoutW(scroll_target, WM_MOUSEWHEEL, wheel_parameters,
                          screen_point, SMTO_ABORTIFHUNG | SMTO_BLOCK,
                          kScrollDispatchTimeoutMs,
                          &message_result) != 0) {
    return true;
  }

  // Explorer 的现代视图重建文件列表时，UI 线程可能短暂阻塞。
  // 同步发送超时不应直接变成失败；改为向同一目标排队发送消息，
  // 再由后续的位置或图像检查判断滚动是否真的发生。
  return PostMessageW(scroll_target, WM_MOUSEWHEEL, wheel_parameters,
                      screen_point) != FALSE;
}

bool queryVerticalScrollState(const LongShotProfileResult& profile,
                              LongShotScrollState& out) {
  out = LongShotScrollState{};
  if (!profile.valid()) {
    return false;
  }

  const HWND scroll_target = reinterpret_cast<HWND>(profile.scroll_target);
  if (scroll_target == nullptr || !IsWindow(scroll_target)) {
    return false;
  }

  SCROLLINFO scroll_info{};
  scroll_info.cbSize = sizeof(scroll_info);
  scroll_info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  if (!GetScrollInfo(scroll_target, SB_VERT, &scroll_info) ||
      scroll_info.nPage == 0) {
    return false;
  }

  // nMax 是包含端点的。结合页面大小，最后一个合法位置为 nMax - nPage + 1。
  // 使用 64 位算术，防止边界情况下发生溢出。
  const std::int64_t last_position =
      static_cast<std::int64_t>(scroll_info.nMax) -
      static_cast<std::int64_t>(scroll_info.nPage) + 1;
  out.position = static_cast<std::int64_t>(scroll_info.nPos);
  out.last_position = last_position;
  out.valid = true;
  return true;
}

}  // longshot_detail 命名空间
}  // qingying 命名空间
