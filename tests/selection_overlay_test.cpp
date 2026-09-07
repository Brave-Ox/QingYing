#include "qingying/overlay/selection_overlay.hpp"

#include <Windows.h>

#include <gtest/gtest.h>

#include <cstdint>

namespace qingying {
namespace {

TEST(SelectionOverlayTest, ShowReturnsWithoutBlockingAndHideIsSilent) {
  SelectionOverlay overlay;
  bool callback_invoked = false;

  ASSERT_TRUE(overlay.show(
      Image{},
      [&callback_invoked](const SelectionResult&) {
        callback_invoked = true;
      }));
  EXPECT_TRUE(overlay.isVisible());

  overlay.hide();
  EXPECT_FALSE(overlay.isVisible());
  EXPECT_FALSE(callback_invoked);
}

TEST(SelectionOverlayTest, DestructorClosesVisibleOverlayWithoutCallback) {
  int callback_count = 0;
  {
    SelectionOverlay overlay;
    ASSERT_TRUE(overlay.show(
        Image{}, [&callback_count](const SelectionResult&) {
          ++callback_count;
        }));
    EXPECT_TRUE(overlay.isVisible());
  }

  EXPECT_EQ(callback_count, 0);
}

TEST(SelectionOverlayTest, HoverInfrastructureDoesNotChangeSilentHide)
{
  SelectionOverlay overlay;
  int callback_count = 0;
  ASSERT_TRUE(overlay.show(
      Image{}, [&callback_count](const SelectionIntent&) {
        ++callback_count;
      }));

  overlay.hide();

  EXPECT_FALSE(overlay.isVisible());
  EXPECT_EQ(callback_count, 0);
}

TEST(SelectionOverlayTest, QueuedLongShotMessagesCanBeAbortedSafely) {
  SelectionOverlay overlay;
  ASSERT_TRUE(overlay.show(Image{}, [](const SelectionResult&) {}));

  Image preview;
  preview.width = 1;
  preview.height = 1;
  preview.pixels = {0xFFFFFFFFu};
  EXPECT_TRUE(overlay.postLongShotPreview(preview));
  EXPECT_TRUE(overlay.postLongShotFinished(true));

  overlay.hide();
  overlay.hide();
  EXPECT_FALSE(overlay.isVisible());
}

TEST(SelectionOverlayTest, WindowDestroyedBeforeOwnerIsSafe) {
  SelectionOverlay overlay;
  ASSERT_TRUE(overlay.show(Image{}, [](const SelectionResult&) {}));

  const HWND hwnd = FindWindowW(L"QingYingSelectionOverlay", nullptr);
  ASSERT_NE(hwnd, nullptr);
  ASSERT_TRUE(DestroyWindow(hwnd));
  EXPECT_FALSE(overlay.isVisible());

  overlay.hide();
  ASSERT_TRUE(overlay.show(Image{}, [](const SelectionResult&) {}));
  overlay.hide();
}

}  // namespace
}  // namespace qingying
