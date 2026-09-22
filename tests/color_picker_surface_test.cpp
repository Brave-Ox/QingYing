#include <Windows.h>

#include <gtest/gtest.h>

#include "annotate/annotation_editor_color_picker.h"
#include "desktop_test_environment.hpp"

namespace qingying {
namespace {

class TestWindow
{
 public:
  explicit TestWindow(HWND owner = nullptr)
      : m_window(CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"",
                                 WS_POPUP | WS_VISIBLE, 0, 0, 2, 2, owner,
                                 nullptr, GetModuleHandleW(nullptr), nullptr))
  {
  }

  ~TestWindow()
  {
    if (m_window != nullptr)
    {
      (void)DestroyWindow(m_window);
    }
  }

  TestWindow(const TestWindow&) = delete;
  TestWindow& operator=(const TestWindow&) = delete;

  HWND get() const noexcept
  {
    return m_window;
  }

 private:
  HWND m_window{nullptr};
};

TEST(ColorPickerSurfaceTest, CoversVirtualDesktopOutsideColorPicker)
{
  const auto desktop = test::probeDesktopCapture();
  if (!desktop.available) GTEST_SKIP() << desktop.diagnostic;

  TestWindow overlay;
  ASSERT_NE(overlay.get(), nullptr);
  TestWindow picker(overlay.get());
  ASSERT_NE(picker.get(), nullptr);

  AnnotationEditorHost host;
  host.window().m_overlay = overlay.get();
  host.colorPicker().m_color_picker = picker.get();

  setColorPickerEyedropping(&host, true);

  ASSERT_TRUE(host.colorPicker().m_color_picker_eyedropping);
  const HWND surface = host.colorPicker().m_color_picker_eyedropper_surface;
  ASSERT_NE(surface, nullptr);
  const POINT sample_point{
      GetSystemMetrics(SM_XVIRTUALSCREEN) +
          GetSystemMetrics(SM_CXVIRTUALSCREEN) / 2,
      GetSystemMetrics(SM_YVIRTUALSCREEN) +
          GetSystemMetrics(SM_CYVIRTUALSCREEN) / 2};
  EXPECT_EQ(WindowFromPoint(sample_point), surface);
  EXPECT_EQ(SendMessageW(surface, WM_SETCURSOR,
                         reinterpret_cast<WPARAM>(surface), HTCLIENT),
            TRUE);

  setColorPickerEyedropping(&host, false);
  EXPECT_EQ(IsWindow(surface), FALSE);
}

}  // namespace
}  // namespace qingying
