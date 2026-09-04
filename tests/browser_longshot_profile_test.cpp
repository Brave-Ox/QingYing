#include "browser_longshot_resolver.h"

#include "builtin_longshot_test_helpers.h"

#include <Windows.h>

#include <gtest/gtest.h>

#include <cstdint>

namespace qingying {
namespace {

constexpr wchar_t kChromiumRootClass[] = L"Chrome_WidgetWin_1";
constexpr wchar_t kChromiumRendererClass[] = L"Chrome_RenderWidgetHostHWND";
constexpr wchar_t kFirefoxRootClass[] = L"MozillaWindowClass";
constexpr wchar_t kFirefoxContentClass[] = L"MozillaContentWindowClass";
constexpr wchar_t kGenericBrowserRootClass[] = L"QingYingBrowserSurface";
constexpr wchar_t kGenericBrowserContentClass[] = L"QingYingBrowserContent";
constexpr wchar_t kOrdinaryRootClass[] = L"QingYingOrdinaryWindow";
constexpr wchar_t kOrdinaryContentClass[] = L"QingYingOrdinaryContent";

bool ensureWindowClass(const wchar_t* class_name, WNDPROC window_proc) {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = window_proc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = class_name;
  if (RegisterClassExW(&window_class) != 0) {
    return true;
  }
  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

LRESULT CALLBACK trackedContentWindowProc(HWND window, UINT message,
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

class TestBrowserWindow {
 public:
  TestBrowserWindow(const wchar_t* root_class, const wchar_t* content_class)
  {
    if (!ensureWindowClass(root_class, DefWindowProcW) ||
        !ensureWindowClass(content_class, trackedContentWindowProc)) {
      return;
    }

    m_root = CreateWindowExW(
        WS_EX_NOACTIVATE, root_class, L"", WS_OVERLAPPED | WS_VISIBLE,
        -10000, -10000, 1000, 800, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (m_root == nullptr) {
      return;
    }

    m_content = CreateWindowExW(
        0, content_class, L"", WS_CHILD | WS_VISIBLE, 40, 120, 800, 600,
        m_root, nullptr, GetModuleHandleW(nullptr), &m_wheel_message_count);
  }

  ~TestBrowserWindow() {
    if (m_content != nullptr) {
      DestroyWindow(m_content);
    }
    if (m_root != nullptr) {
      DestroyWindow(m_root);
    }
  }

  TestBrowserWindow(const TestBrowserWindow&) = delete;
  TestBrowserWindow& operator=(const TestBrowserWindow&) = delete;

  HWND root() const noexcept { return m_root; }
  HWND content() const noexcept { return m_content; }
  int wheelMessageCount() const noexcept { return m_wheel_message_count; }

  LongShotRequest requestForContent() const {
    const RECT rect = contentScreenRect();
    return {reinterpret_cast<std::uintptr_t>(m_root), rect.left, rect.top,
            rect.right - rect.left, rect.bottom - rect.top};
  }

  LongShotRequest requestForPartialContent() const {
    const RECT rect = contentScreenRect();
    return {reinterpret_cast<std::uintptr_t>(m_root), rect.left + 20,
            rect.top + 30, 500, 300};
  }

 private:
  RECT contentScreenRect() const {
    RECT rect{};
    GetClientRect(m_content, &rect);
    POINT top_left{rect.left, rect.top};
    POINT bottom_right{rect.right, rect.bottom};
    ClientToScreen(m_content, &top_left);
    ClientToScreen(m_content, &bottom_right);
    return {top_left.x, top_left.y, bottom_right.x, bottom_right.y};
  }

  HWND m_root{nullptr};
  HWND m_content{nullptr};
  int m_wheel_message_count{0};
};

}  // namespace

TEST(BrowserLongShotProfileTest, ResolvesChromiumRendererViewport) {
  TestBrowserWindow window(kChromiumRootClass, kChromiumRendererClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);

  const LongShotRequest request = window.requestForContent();
  LongShotProfileResult result;

  ASSERT_TRUE(longshot_detail::resolveBrowserTarget(request.owner_window,
                                                     request, result));
  EXPECT_EQ(result.scroll_target,
            reinterpret_cast<std::uintptr_t>(window.content()));
  EXPECT_TRUE(result.containsSelection(request.x, request.y, request.width,
                                       request.height));
}

TEST(BrowserLongShotProfileTest, RejectsFirefoxUntilFirefoxAdapterExists) {
  TestBrowserWindow window(kFirefoxRootClass, kFirefoxContentClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);

  const LongShotRequest request = window.requestForContent();
  LongShotProfileResult result;

  EXPECT_FALSE(longshot_detail::resolveBrowserTarget(request.owner_window,
                                                      request, result));
  EXPECT_FALSE(result.valid());
}

TEST(BrowserLongShotProfileTest, RejectsGenericBrowserUntilModeExists) {
  TestBrowserWindow window(kGenericBrowserRootClass,
                           kGenericBrowserContentClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);

  const LongShotRequest request = window.requestForContent();
  LongShotProfileResult result;

  EXPECT_FALSE(longshot_detail::resolveBrowserTarget(request.owner_window,
                                                      request, result));
  EXPECT_FALSE(result.valid());
}

TEST(BrowserLongShotProfileTest, RejectsOrdinaryDesktopWindow) {
  TestBrowserWindow window(kOrdinaryRootClass, kOrdinaryContentClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);

  const LongShotRequest request = window.requestForContent();
  LongShotProfileResult result;

  EXPECT_FALSE(longshot_detail::resolveBrowserTarget(request.owner_window,
                                                      request, result));
  EXPECT_FALSE(result.valid());
}

TEST(BrowserLongShotPluginTest, LoadsAndScrollsThroughBrowserContentTarget) {
  TestBrowserWindow window(kChromiumRootClass, kChromiumRendererClass);
  ASSERT_NE(window.root(), nullptr);
  ASSERT_NE(window.content(), nullptr);

  const LongShotRequest request = window.requestForContent();
  test_support::BuiltinLongShotProfileContext context("builtin.browser");
  ASSERT_TRUE(context.ready());

  LongShotProfileResult result;
  ASSERT_TRUE(context.profile().resolve(request, result));
  EXPECT_EQ(result.scroll_target,
            reinterpret_cast<std::uintptr_t>(window.content()));
  EXPECT_TRUE(context.profile().scrollDown(request, result));
  EXPECT_EQ(window.wheelMessageCount(), 1);

  LongShotScrollState state;
  EXPECT_FALSE(context.profile().queryScrollState(result, state));
  EXPECT_FALSE(state.valid);
}

}  // namespace qingying
