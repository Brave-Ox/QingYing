#include "qingying/ui/modern_toolbar.hpp"

#include <gtest/gtest.h>

namespace qingying {

TEST(ModernToolbarTest, CompactMetricsMatchReferenceBar)
{
  EXPECT_EQ(DefaultModernToolbarMetrics.item_size, 28);
  EXPECT_EQ(DefaultModernToolbarMetrics.gap, 4);
  EXPECT_EQ(modernToolbarHeight(DefaultModernToolbarMetrics), 44);
}

TEST(ModernToolbarTest, DefaultWidthFitsFourSelectionActions)
{
  const int width = modernToolbarWidth(4, 0, DefaultModernToolbarMetrics);
  EXPECT_EQ(width, DefaultModernToolbarMetrics.bar_padding * 2 +
                       4 * DefaultModernToolbarMetrics.item_size +
                       3 * DefaultModernToolbarMetrics.gap);
}

TEST(ModernToolbarTest, IconLabelsAreChineseTooltips)
{
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Copy), L"\x590D\x5236");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Save), L"\x4E0B\x8F7D\x56FE\x7247");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Edit), L"\x7F16\x8F91");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Pin), L"\x9489\x56FE");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Rectangle), L"\x77E9\x5F62");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Ellipse), L"\x692D\x5706");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Arrow), L"\x7BAD\x5934");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Pen), L"\x753B\x7B14");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Mosaic), L"\x9A6C\x8D5B\x514B");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Text), L"\x6587\x5B57");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Undo), L"\x64A4\x9500");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Confirm), L"\x5B8C\x6210");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Cancel), L"\x53D6\x6D88");
}

}  // namespace qingying
