#include "builtin_longshot_test_helpers.h"

#include "qingying/longshot/longshot_profile_registry.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

namespace qingying {
namespace {

constexpr wchar_t kChromeWindowClass[] = L"Chrome_WidgetWin_1";
constexpr wchar_t kRendererWindowClass[] = L"Chrome_RenderWidgetHostHWND";
constexpr wchar_t kD3DSurfaceWindowClass[] = L"Intermediate D3D Window";
constexpr wchar_t kUnsupportedWindowClass[] =
    L"QingYingChromeUnsupportedWindow";

bool ensureWindowClass(const wchar_t* class_name, WNDPROC window_proc) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = window_proc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = class_name;
  if (RegisterClassExW(&wc) != 0) {
    return true;
  }
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

LRESULT CALLBACK rendererWindowProc(HWND window, UINT message, WPARAM w_param,
                                    LPARAM l_param) {
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
    SetWindowLongPtrW(window, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(create->lpCreateParams));
  } else if (message == WM_MOUSEWHEEL) {
    auto* wheel_count =
        reinterpret_cast<int*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (wheel_count != nullptr) {
      ++(*wheel_count);
    }
  }
  return DefWindowProcW(window, message, w_param, l_param);
}

class TestChromeWindow {
 public:
  explicit TestChromeWindow(const wchar_t* root_class = kChromeWindowClass,
                            bool create_renderer = true) {
    if (!ensureWindowClass(root_class, DefWindowProcW) ||
        !ensureWindowClass(kRendererWindowClass, rendererWindowProc) ||
        !ensureWindowClass(kD3DSurfaceWindowClass, rendererWindowProc)) {
      return;
    }

    root_ = CreateWindowExW(
        WS_EX_NOACTIVATE, root_class, L"", WS_OVERLAPPED | WS_VISIBLE,
        -10000, -10000, 900, 700, nullptr, nullptr, GetModuleHandleW(nullptr),
        nullptr);
    if (root_ == nullptr) {
      return;
    }

    if (create_renderer) {
      renderer_ = CreateWindowExW(
          0, kRendererWindowClass, L"", WS_CHILD | WS_VISIBLE, 30, 80, 700,
          500, root_, nullptr, GetModuleHandleW(nullptr),
          &wheel_message_count_);
    } else {
      renderer_ = CreateWindowExW(
          0, kD3DSurfaceWindowClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 900,
          700, root_, nullptr, GetModuleHandleW(nullptr),
          &wheel_message_count_);
    }
  }

  ~TestChromeWindow() {
    if (renderer_ != nullptr) {
      DestroyWindow(renderer_);
    }
    if (root_ != nullptr) {
      DestroyWindow(root_);
    }
  }

  HWND root() const { return root_; }
  HWND renderer() const { return renderer_; }
  int wheelMessageCount() const { return wheel_message_count_; }

  RECT rendererScreenRect() const {
    RECT rect{};
    GetClientRect(renderer_, &rect);
    POINT top_left{rect.left, rect.top};
    POINT bottom_right{rect.right, rect.bottom};
    ClientToScreen(renderer_, &top_left);
    ClientToScreen(renderer_, &bottom_right);
    return {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  }

 private:
  HWND root_{nullptr};
  HWND renderer_{nullptr};
  int wheel_message_count_{0};
};

LongShotRequest requestForRenderer(const TestChromeWindow& window) {
  const RECT rect = window.rendererScreenRect();
  return {reinterpret_cast<std::uintptr_t>(window.root()), rect.left, rect.top,
          rect.right - rect.left, rect.bottom - rect.top};
}

}  // namespace

TEST(ChromeLongShotProfileTest, ResolvesVisibleRendererContentArea) {
  TestChromeWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.renderer(), nullptr);
  const LongShotRequest request = requestForRenderer(window);
  test_support::BuiltinLongShotProfileContext context("builtin.chrome");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;

  ASSERT_TRUE(context.profile().resolve(request, result));
  EXPECT_EQ(result.scroll_target,
            reinterpret_cast<std::uintptr_t>(window.renderer()));
  EXPECT_TRUE(result.containsSelection(request.x, request.y, request.width,
                                       request.height));
}

TEST(ChromeLongShotProfileTest, RejectsSelectionOutsidePageViewport) {
  TestChromeWindow window;
  ASSERT_NE(window.root(), nullptr);
  const RECT rect = window.rendererScreenRect();
  const LongShotRequest request{
      reinterpret_cast<std::uintptr_t>(window.root()), rect.left - 1, rect.top,
      rect.right - rect.left, rect.bottom - rect.top};
  test_support::BuiltinLongShotProfileContext context("builtin.chrome");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;

  EXPECT_FALSE(context.profile().resolve(request, result));
  EXPECT_FALSE(result.valid());
}

TEST(ChromeLongShotProfileTest, FallsBackToVisibleD3DSurface) {
  TestChromeWindow window(kChromeWindowClass, false);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.renderer(), nullptr);
  const LongShotRequest request = requestForRenderer(window);
  test_support::BuiltinLongShotProfileContext context("builtin.chrome");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;

  ASSERT_TRUE(context.profile().resolve(request, result));
  EXPECT_EQ(result.scroll_target,
            reinterpret_cast<std::uintptr_t>(window.renderer()));
}

TEST(ChromeLongShotProfileTest, SendsWheelToResolvedRenderer) {
  TestChromeWindow window;
  ASSERT_NE(window.root(), nullptr);
  const LongShotRequest request = requestForRenderer(window);
  test_support::BuiltinLongShotProfileContext context("builtin.chrome");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;
  ASSERT_TRUE(context.profile().resolve(request, result));

  EXPECT_TRUE(context.profile().scrollDown(request, result));
  EXPECT_EQ(window.wheelMessageCount(), 1);
}

TEST(ChromeLongShotProfileTest, UsesImageOverlapWhenNoNativeScrollStateExists) {
  TestChromeWindow window;
  ASSERT_NE(window.root(), nullptr);
  const LongShotRequest request = requestForRenderer(window);
  test_support::BuiltinLongShotProfileContext context("builtin.chrome");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;
  ASSERT_TRUE(context.profile().resolve(request, result));
  LongShotScrollState state;

  EXPECT_FALSE(context.profile().queryScrollState(result, state));
}

TEST(ChromeLongShotProfileTest, RejectsNonChromeWindow) {
  TestChromeWindow window(kUnsupportedWindowClass);
  ASSERT_NE(window.root(), nullptr);
  const LongShotRequest request = requestForRenderer(window);
  test_support::BuiltinLongShotProfileContext context("builtin.chrome");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;

  EXPECT_FALSE(context.profile().resolve(request, result));
  EXPECT_FALSE(result.valid());
}

}  // namespace qingying
