#include "qingying/window/window_detector.hpp"

#include <gtest/gtest.h>

#include "window_query_helpers.h"

namespace qingying {

namespace {

constexpr wchar_t kTestWindowClass[] = L"QingYingTestWindow";

// 在测试进程里创建一个可见窗口，用于验证自身进程窗口会被过滤。
class TestWindow {
 public:
  TestWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kTestWindowClass;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, kTestWindowClass, L"", WS_OVERLAPPED | WS_VISIBLE, 100, 100, 300,
        200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  }

  ~TestWindow() {
    if (hwnd_ != nullptr) {
      DestroyWindow(hwnd_);
    }
  }

  HWND hwnd() const { return hwnd_; }

 private:
  HWND hwnd_{nullptr};
};

}  // namespace

TEST(WindowDetectorTest, WindowRectBasics) {
  const WindowRect rect{10, 20, 110, 70};
  EXPECT_EQ(rect.width(), 100);
  EXPECT_EQ(rect.height(), 50);
  EXPECT_FALSE(rect.empty());

  const WindowRect empty{10, 20, 10, 20};
  EXPECT_TRUE(empty.empty());
}

TEST(WindowDetectorTest, DetectOffscreenPointReturnsFalse) {
  WindowDetector detector;
  HWND window = nullptr;
  WindowRect rect;
  EXPECT_FALSE(detector.detectAt(-10000, -10000, window, rect));
}

TEST(WindowDetectorTest, OwnProcessWindowIsNotSnappable) {
  TestWindow window;
  ASSERT_NE(window.hwnd(), nullptr);
  EXPECT_FALSE(WindowDetector::isSnappable(window.hwnd()));
}

TEST(WindowDetectorTest, NullWindowIsNotSnappable) {
  EXPECT_FALSE(WindowDetector::isSnappable(nullptr));
}

TEST(WindowDetectorTest, TaskbarClassAllowlistAcceptsOnlyTaskbarClasses)
{
  EXPECT_TRUE(window_detail::isTaskbarWindowClass(L"Shell_TrayWnd"));
  EXPECT_TRUE(window_detail::isTaskbarWindowClass(L"Shell_SecondaryTrayWnd"));
  EXPECT_FALSE(window_detail::isTaskbarWindowClass(L"Shell_TrayWndExtra"));
  EXPECT_FALSE(window_detail::isTaskbarWindowClass(L"WorkerW"));
  EXPECT_FALSE(window_detail::isTaskbarWindowClass(L"QingYingTestWindow"));
}

TEST(WindowDetectorTest, SnapshotAtFindsVisibleTaskbarWhenDesktopIsAvailable)
{
  const HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
  if (taskbar == nullptr || IsWindowVisible(taskbar) == FALSE)
  {
    GTEST_SKIP() << "interactive Windows taskbar is unavailable";
  }

  RECT taskbar_rect{};
  ASSERT_NE(GetWindowRect(taskbar, &taskbar_rect), FALSE);
  ASSERT_LT(taskbar_rect.left, taskbar_rect.right);
  ASSERT_LT(taskbar_rect.top, taskbar_rect.bottom);

  WindowDetector detector;
  HWND detected_window = nullptr;
  WindowRect detected_rect;
  ASSERT_TRUE(detector.snapshotAt((taskbar_rect.left + taskbar_rect.right) / 2,
                                  (taskbar_rect.top + taskbar_rect.bottom) / 2,
                                  detected_window, detected_rect));
  EXPECT_EQ(detected_window, taskbar);
  EXPECT_EQ(detected_rect.left, taskbar_rect.left);
  EXPECT_EQ(detected_rect.top, taskbar_rect.top);
  EXPECT_EQ(detected_rect.right, taskbar_rect.right);
  EXPECT_EQ(detected_rect.bottom, taskbar_rect.bottom);
}

}  // namespace qingying
