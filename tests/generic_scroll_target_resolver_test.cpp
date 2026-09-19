#include "generic_scroll_target_resolver.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

namespace qingying {
namespace {

constexpr wchar_t kGenericTargetClass[] =
    L"QingYingGenericScrollTargetResolverTest";

class ScopedPhysicalCoordinates {
 public:
  ScopedPhysicalCoordinates() {
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == nullptr) return;
    set_context_ = reinterpret_cast<SetContextFn>(
        GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
    if (set_context_ != nullptr) {
      previous_ = set_context_(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
  }

  ~ScopedPhysicalCoordinates() {
    if (set_context_ != nullptr && previous_ != nullptr) {
      (void)set_context_(previous_);
    }
  }

 private:
  using SetContextFn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
  SetContextFn set_context_{nullptr};
  DPI_AWARENESS_CONTEXT previous_{nullptr};
};

bool ensureTargetClass() {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = kGenericTargetClass;
  if (RegisterClassExW(&window_class) != 0) return true;
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

ScreenPhysicalRect clientScreenRect(HWND window) {
  ScopedPhysicalCoordinates physical_coordinates;
  RECT rect{};
  EXPECT_TRUE(GetClientRect(window, &rect));
  POINT top_left{rect.left, rect.top};
  POINT bottom_right{rect.right, rect.bottom};
  EXPECT_TRUE(ClientToScreen(window, &top_left));
  EXPECT_TRUE(ClientToScreen(window, &bottom_right));
  return {top_left.x, top_left.y, bottom_right.x - top_left.x,
          bottom_right.y - top_left.y};
}

LongShotRequest requestFor(HWND owner, ScreenPhysicalRect selection) {
  return {reinterpret_cast<std::uintptr_t>(owner), selection};
}

ScreenPhysicalRect inset(ScreenPhysicalRect rect, int amount) {
  rect.x += amount;
  rect.y += amount;
  rect.width -= amount * 2;
  rect.height -= amount * 2;
  return rect;
}

class TargetWindowTree {
 public:
  TargetWindowTree(int x = -10000, int y = -10000, int width = 640,
                   int height = 480) {
    if (!ensureTargetClass()) return;
    root_ = CreateWindowExW(
        WS_EX_NOACTIVATE, kGenericTargetClass, L"", WS_POPUP | WS_VISIBLE,
        x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr),
        nullptr);
  }

  ~TargetWindowTree() {
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
      if (IsWindow(*it)) DestroyWindow(*it);
    }
    if (IsWindow(root_)) DestroyWindow(root_);
  }

  HWND addChild(HWND parent, int x, int y, int width, int height) {
    HWND child = CreateWindowExW(
        0, kGenericTargetClass, L"", WS_CHILD | WS_VISIBLE, x, y, width,
        height, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (child != nullptr) children_.push_back(child);
    return child;
  }

  HWND root() const noexcept { return root_; }

 private:
  HWND root_{nullptr};
  std::vector<HWND> children_;
};

constexpr longshot_detail::GenericScrollTargetPolicy kAllowTestProcess{0};

}  // namespace

TEST(GenericScrollTargetResolverTest,
     ResolvesNestedRowsToTheirDeepestCommonContentWindow) {
  TargetWindowTree windows;
  ASSERT_NE(windows.root(), nullptr);
  const HWND pane = windows.addChild(windows.root(), 20, 30, 520, 360);
  const HWND viewport = windows.addChild(pane, 10, 12, 480, 300);
  ASSERT_NE(viewport, nullptr);
  ASSERT_NE(windows.addChild(viewport, 0, 0, 480, 150), nullptr);
  ASSERT_NE(windows.addChild(viewport, 0, 150, 480, 150), nullptr);
  const auto selection = inset(clientScreenRect(viewport), 2);

  const auto resolution = longshot_detail::resolveGenericScrollTarget(
      requestFor(windows.root(), selection), kAllowTestProcess);

  ASSERT_TRUE(resolution.resolved());
  EXPECT_EQ(resolution.target.target_window,
            reinterpret_cast<std::uintptr_t>(viewport));
  EXPECT_EQ(resolution.target.content, clientScreenRect(viewport));
  EXPECT_EQ(resolution.target.anchor_x, selection.x + selection.width / 2);
  EXPECT_EQ(resolution.target.anchor_y, selection.y + selection.height / 2);
}

TEST(GenericScrollTargetResolverTest,
     UsesOwnerWhenVisualSurfaceHasNoIndependentChildWindow) {
  TargetWindowTree windows;
  ASSERT_NE(windows.root(), nullptr);
  const auto selection = inset(clientScreenRect(windows.root()), 24);

  const auto resolution = longshot_detail::resolveGenericScrollTarget(
      requestFor(windows.root(), selection), kAllowTestProcess);

  ASSERT_TRUE(resolution.resolved());
  EXPECT_EQ(resolution.target.target_window,
            reinterpret_cast<std::uintptr_t>(windows.root()));
  EXPECT_TRUE(longshot_detail::validateGenericScrollTarget(
      requestFor(windows.root(), selection), resolution.target,
      kAllowTestProcess));
}

TEST(GenericScrollTargetResolverTest, RejectsQingYingOwnedWindowsByDefault) {
  TargetWindowTree windows;
  ASSERT_NE(windows.root(), nullptr);
  const auto selection = inset(clientScreenRect(windows.root()), 20);

  const auto resolution = longshot_detail::resolveGenericScrollTarget(
      requestFor(windows.root(), selection));

  EXPECT_EQ(resolution.status,
            longshot_detail::GenericScrollTargetStatus::SelfUi);
  EXPECT_FALSE(resolution.target.valid());
}

TEST(GenericScrollTargetResolverTest, RejectsSelectionAcrossSiblingPanes) {
  TargetWindowTree windows;
  ASSERT_NE(windows.root(), nullptr);
  const HWND left = windows.addChild(windows.root(), 20, 30, 250, 350);
  const HWND right = windows.addChild(windows.root(), 270, 30, 250, 350);
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  const auto left_rect = clientScreenRect(left);
  const auto right_rect = clientScreenRect(right);
  const ScreenPhysicalRect selection{
      left_rect.x + 5, left_rect.y + 5,
      right_rect.x + right_rect.width - left_rect.x - 10,
      left_rect.height - 10};

  const auto resolution = longshot_detail::resolveGenericScrollTarget(
      requestFor(windows.root(), selection), kAllowTestProcess);

  EXPECT_EQ(resolution.status,
            longshot_detail::GenericScrollTargetStatus::CrossPane);
  EXPECT_FALSE(resolution.resolved());
}

TEST(GenericScrollTargetResolverTest,
     InvalidatesResolvedTargetAfterContentWindowIsDestroyed) {
  TargetWindowTree windows;
  ASSERT_NE(windows.root(), nullptr);
  const HWND content = windows.addChild(windows.root(), 30, 40, 400, 300);
  ASSERT_NE(content, nullptr);
  const auto selection = inset(clientScreenRect(content), 4);
  const LongShotRequest request = requestFor(windows.root(), selection);
  const auto resolution = longshot_detail::resolveGenericScrollTarget(
      request, kAllowTestProcess);
  ASSERT_TRUE(resolution.resolved());
  ASSERT_EQ(resolution.target.target_window,
            reinterpret_cast<std::uintptr_t>(content));

  ASSERT_TRUE(DestroyWindow(content));

  EXPECT_FALSE(longshot_detail::validateGenericScrollTarget(
      request, resolution.target, kAllowTestProcess));
}

TEST(GenericScrollTargetResolverTest,
     InvalidatesResolvedTargetAfterItsPhysicalBoundsMove) {
  TargetWindowTree windows;
  ASSERT_NE(windows.root(), nullptr);
  const HWND content = windows.addChild(windows.root(), 30, 40, 400, 300);
  ASSERT_NE(content, nullptr);
  const auto selection = inset(clientScreenRect(content), 4);
  const LongShotRequest request = requestFor(windows.root(), selection);
  const auto resolution = longshot_detail::resolveGenericScrollTarget(
      request, kAllowTestProcess);
  ASSERT_TRUE(resolution.resolved());

  ASSERT_TRUE(SetWindowPos(content, nullptr, 31, 40, 400, 300,
                           SWP_NOACTIVATE | SWP_NOZORDER));

  EXPECT_FALSE(longshot_detail::validateGenericScrollTarget(
      request, resolution.target, kAllowTestProcess));
}

TEST(GenericScrollTargetResolverTest,
     PreservesNegativePhysicalDesktopCoordinatesWithoutScaling) {
  TargetWindowTree windows(-20000, -18000, 700, 520);
  ASSERT_NE(windows.root(), nullptr);
  const HWND content = windows.addChild(windows.root(), 37, 53, 411, 307);
  ASSERT_NE(content, nullptr);
  const auto content_rect = clientScreenRect(content);
  ASSERT_LT(content_rect.x, 0);
  ASSERT_LT(content_rect.y, 0);
  const auto selection = inset(content_rect, 7);

  const auto resolution = longshot_detail::resolveGenericScrollTarget(
      requestFor(windows.root(), selection), kAllowTestProcess);

  ASSERT_TRUE(resolution.resolved());
  EXPECT_EQ(resolution.target.content, content_rect);
  EXPECT_EQ(resolution.target.anchor_x, selection.x + selection.width / 2);
  EXPECT_EQ(resolution.target.anchor_y, selection.y + selection.height / 2);
}

TEST(GenericScrollTargetResolverTest, RejectsSelectionOutsideOwnerClientArea) {
  TargetWindowTree windows;
  ASSERT_NE(windows.root(), nullptr);
  auto selection = clientScreenRect(windows.root());
  selection.x -= 1;

  const auto resolution = longshot_detail::resolveGenericScrollTarget(
      requestFor(windows.root(), selection), kAllowTestProcess);

  EXPECT_EQ(
      resolution.status,
      longshot_detail::GenericScrollTargetStatus::SelectionOutsideOwner);
}

TEST(GenericScrollTargetResolverTest,
     RejectsOverflowingSelectionBeforeWindowHitTesting) {
  TargetWindowTree windows;
  ASSERT_NE(windows.root(), nullptr);
  const LongShotRequest request{
      reinterpret_cast<std::uintptr_t>(windows.root()),
      (std::numeric_limits<int>::max)() - 3, 0, 10, 10};

  const auto resolution = longshot_detail::resolveGenericScrollTarget(
      request, kAllowTestProcess);

  EXPECT_EQ(resolution.status,
            longshot_detail::GenericScrollTargetStatus::InvalidRequest);
}

}  // namespace qingying
