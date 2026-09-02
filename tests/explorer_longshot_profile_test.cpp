#include "qingying/capture/capture_engine.hpp"
#include "builtin_longshot_test_helpers.h"
#include "qingying/longshot/longshot_engine.hpp"
#include "qingying/longshot/longshot_profile_registry.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

namespace qingying {

namespace {

constexpr wchar_t kExplorerWindowClass[] = L"CabinetWClass";
constexpr wchar_t kExplorerPaneClass[] = L"DirectUIHWND";
constexpr wchar_t kUnsupportedWindowClass[] =
    L"QingYingExplorerUnsupportedWindow";

bool ensureWindowClass(const wchar_t* class_name, WNDPROC window_proc);

LRESULT CALLBACK trackedPaneWindowProc(HWND window, UINT message,
                                       WPARAM w_param, LPARAM l_param) {
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

class TestExplorerWindow {
 public:
  explicit TestExplorerWindow(const wchar_t* root_class =
                                  kExplorerWindowClass) {
    if (!ensureWindowClass(root_class, DefWindowProcW) ||
        !ensureWindowClass(kExplorerPaneClass, trackedPaneWindowProc)) {
      return;
    }

    root_ = CreateWindowExW(
        WS_EX_NOACTIVATE, root_class, L"",
        WS_OVERLAPPED | WS_VISIBLE, -10000, -10000, 700, 500, nullptr,
        nullptr, GetModuleHandleW(nullptr), nullptr);
    if (root_ == nullptr) {
      return;
    }

    pane_ = CreateWindowExW(
        0, kExplorerPaneClass, L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL, 30, 40, 500, 300, root_, nullptr,
        GetModuleHandleW(nullptr), &wheel_message_count_);
  }

  ~TestExplorerWindow() {
    if (pane_ != nullptr) {
      DestroyWindow(pane_);
    }
    if (root_ != nullptr) {
      DestroyWindow(root_);
    }
  }

  HWND root() const { return root_; }
  HWND pane() const { return pane_; }
  int wheelMessageCount() const { return wheel_message_count_; }

  RECT paneScreenRect() const {
    RECT rect{};
    GetClientRect(pane_, &rect);
    POINT top_left{rect.left, rect.top};
    POINT bottom_right{rect.right, rect.bottom};
    ClientToScreen(pane_, &top_left);
    ClientToScreen(pane_, &bottom_right);
    return {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  }

  void setVerticalScrollInfo(int minimum, int maximum, UINT page,
                             int position) {
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = minimum;
    info.nMax = maximum;
    info.nPage = page;
    info.nPos = position;
    SetScrollInfo(pane_, SB_VERT, &info, TRUE);
  }

 private:
  HWND root_{nullptr};
  HWND pane_{nullptr};
  int wheel_message_count_{0};
};

LongShotRequest requestForPane(const TestExplorerWindow& window) {
  const RECT rect = window.paneScreenRect();
  return {reinterpret_cast<std::uintptr_t>(window.root()), rect.left, rect.top,
          rect.right - rect.left, rect.bottom - rect.top};
}

}  // 匿名命名空间

TEST(ExplorerLongShotProfileTest, ResolvesSelectionToContainingContentPane) {
  TestExplorerWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.pane(), nullptr);

  const LongShotRequest request = requestForPane(window);
  test_support::BuiltinLongShotProfileContext context("builtin.explorer");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;

  ASSERT_TRUE(context.profile().resolve(request, result));
  EXPECT_EQ(result.scroll_target,
            reinterpret_cast<std::uintptr_t>(window.pane()));
  EXPECT_TRUE(result.containsSelection(request.x, request.y, request.width,
                                       request.height));
}

TEST(ExplorerLongShotProfileTest, RejectsSelectionMostlyOutsideContentPane) {
  TestExplorerWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.pane(), nullptr);

  const RECT rect = window.paneScreenRect();
  const LongShotRequest request{
      reinterpret_cast<std::uintptr_t>(window.root()), rect.left - 101,
      rect.top, rect.right - rect.left, rect.bottom - rect.top};
  test_support::BuiltinLongShotProfileContext context("builtin.explorer");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;

  EXPECT_FALSE(context.profile().resolve(request, result));
  EXPECT_FALSE(result.valid());
}

TEST(ExplorerLongShotProfileTest,
     AcceptsSmallNonScrollableEdgeAroundContentPane) {
  TestExplorerWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.pane(), nullptr);

  const RECT rect = window.paneScreenRect();
  const LongShotRequest request{
      reinterpret_cast<std::uintptr_t>(window.root()), rect.left,
      rect.top - 20, rect.right - rect.left, rect.bottom - rect.top + 20};
  test_support::BuiltinLongShotProfileContext context("builtin.explorer");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;

  EXPECT_TRUE(context.profile().resolve(request, result));
  EXPECT_FALSE(result.containsSelection(request.x, request.y, request.width,
                                        request.height));
}

TEST(ExplorerLongShotProfileTest, RejectsSelectionSpanningSeparatePane) {
  TestExplorerWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.pane(), nullptr);

  const RECT rect = window.paneScreenRect();
  const LongShotRequest request{
      reinterpret_cast<std::uintptr_t>(window.root()), rect.left - 100,
      rect.top - 100, rect.right - rect.left + 200,
      rect.bottom - rect.top + 200};
  test_support::BuiltinLongShotProfileContext context("builtin.explorer");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;

  EXPECT_FALSE(context.profile().resolve(request, result));
  EXPECT_FALSE(result.valid());
}

TEST(ExplorerLongShotProfileTest, SendsWheelOnlyToResolvedPane) {
  TestExplorerWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.pane(), nullptr);

  const LongShotRequest request = requestForPane(window);
  test_support::BuiltinLongShotProfileContext context("builtin.explorer");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;
  ASSERT_TRUE(context.profile().resolve(request, result));

  EXPECT_TRUE(context.profile().scrollDown(request, result));
  EXPECT_EQ(window.wheelMessageCount(), 1);
}

TEST(ExplorerLongShotProfileTest, ReadsVerticalScrollState) {
  TestExplorerWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.pane(), nullptr);
  window.setVerticalScrollInfo(0, 99, 20, 80);

  const LongShotRequest request = requestForPane(window);
  test_support::BuiltinLongShotProfileContext context("builtin.explorer");
  ASSERT_TRUE(context.ready());
  LongShotProfileResult result;
  ASSERT_TRUE(context.profile().resolve(request, result));

  LongShotScrollState state;
  ASSERT_TRUE(context.profile().queryScrollState(result, state));
  EXPECT_TRUE(state.valid);
  EXPECT_EQ(state.position, 80);
  EXPECT_TRUE(state.atBottom());
}

TEST(LongShotProfileRegistryTest, ResolvesAnExplicitlyRegisteredProfile) {
  TestExplorerWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.pane(), nullptr);

  const LongShotRequest request = requestForPane(window);
  test_support::BuiltinLongShotProfileContext context("builtin.explorer");
  ASSERT_TRUE(context.ready());
  LongShotProfileRegistry registry = context.takeRegistry();
  LongShotProfileResult result;

  const LongShotProfile* profile = registry.resolve(request, result);
  ASSERT_NE(profile, nullptr);
  EXPECT_STREQ(profile->name(), "builtin.explorer");
  EXPECT_EQ(result.scroll_target,
            reinterpret_cast<std::uintptr_t>(window.pane()));
}

TEST(LongShotProfileRegistryTest, EmptyRegistryRejectsSupportedWindow) {
  TestExplorerWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.pane(), nullptr);

  const LongShotRequest request = requestForPane(window);
  LongShotProfileRegistry registry;
  LongShotProfileResult result;

  EXPECT_EQ(registry.resolve(request, result), nullptr);
  EXPECT_FALSE(result.valid());
}

TEST(LongShotProfileRegistryTest, RejectsUnsupportedOwnerWindow) {
  TestExplorerWindow window(kUnsupportedWindowClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.pane(), nullptr);

  const LongShotRequest request = requestForPane(window);
  LongShotProfileRegistry registry;
  LongShotProfileResult result;

  EXPECT_EQ(registry.resolve(request, result), nullptr);
  EXPECT_FALSE(result.valid());
}

TEST(LongShotEngineProfileIntegrationTest,
     StopsCleanlyWhenExplorerWheelDoesNotMove) {
  TestExplorerWindow window;
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.pane(), nullptr);
  window.setVerticalScrollInfo(0, 99, 20, 10);

  const LongShotRequest request = requestForPane(window);
  test_support::BuiltinLongShotProfileContext context("builtin.explorer");
  ASSERT_TRUE(context.ready());
  CaptureEngine capture;
  LongShotEngine engine(capture, context.takeRegistry());
  Image out;

  const ActionResult result = engine.captureSelection(request, out);

  EXPECT_TRUE(result.ok);
  EXPECT_FALSE(out.empty());
  EXPECT_EQ(out.width, request.width);
  EXPECT_EQ(out.height, request.height);
  EXPECT_EQ(window.wheelMessageCount(), 1);
}

}  // qingying 命名空间
