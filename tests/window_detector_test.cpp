#include "qingying/window/window_detector.hpp"

#include <gtest/gtest.h>

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

}  // namespace qingying
