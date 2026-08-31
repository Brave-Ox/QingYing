#include "qingying/overlay/selection_overlay.hpp"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace qingying
