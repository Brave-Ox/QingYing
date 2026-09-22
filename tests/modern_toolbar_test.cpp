#include "qingying/ui/modern_toolbar.hpp"

#include <cstdint>

#include <gtest/gtest.h>

namespace qingying {

TEST(ModernToolbarTest, RenderingBackendCanBePreparedBeforeInteractiveUse)
{
  EXPECT_TRUE(prepareModernToolbarRendering());
}

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

TEST(ModernToolbarTest, DefaultWidthFitsSixSelectionActionsWithDivider)
{
  const ModernToolbarMetrics metrics = DefaultModernToolbarMetrics;
  const int extra = metrics.divider_gap - metrics.gap;
  const int width = modernToolbarWidth(6, extra, metrics);
  EXPECT_EQ(width, metrics.bar_padding * 2 + 6 * metrics.item_size +
                       5 * metrics.gap + extra);
}

TEST(ModernToolbarTest, IconsSitOnWhiteBarWithoutDarkButtons)
{
  // PixPin 风格：浅色条上直接放深色线标，空闲态不再铺深灰方钮。
  const ModernToolbarColors colors = DefaultModernToolbarColors;
  const int bar_sum = GetRValue(colors.bar_fill) + GetGValue(colors.bar_fill) +
                      GetBValue(colors.bar_fill);
  const int icon_sum =
      GetRValue(colors.icon) + GetGValue(colors.icon) + GetBValue(colors.icon);
  const int label_sum = GetRValue(colors.label) + GetGValue(colors.label) +
                        GetBValue(colors.label);
  const int selected_sum = GetRValue(colors.selected_fill) +
                           GetGValue(colors.selected_fill) +
                           GetBValue(colors.selected_fill);
  const int hover_sum = GetRValue(colors.hover_fill) +
                        GetGValue(colors.hover_fill) +
                        GetBValue(colors.hover_fill);
  EXPECT_GT(bar_sum, 720);
  EXPECT_LT(icon_sum, 250);
  EXPECT_LT(label_sum, 250);
  EXPECT_GE(bar_sum - icon_sum, 500);
  EXPECT_GT(selected_sum, 650);
  EXPECT_GT(hover_sum, 680);
  EXPECT_NE(colors.button_fill, colors.bar_fill);
  EXPECT_GT(GetRValue(colors.button_fill), 240);
  EXPECT_GT(GetGValue(colors.button_fill), 240);
  EXPECT_GT(GetBValue(colors.button_fill), 240);
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
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::StrokeWidth), L"\x7C97\x7EC6");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Text), L"\x6587\x5B57");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Geometry), L"\x51E0\x4F55");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Fill), L"\x586B\x5145");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::LineSolid), L"\x5B9E\x7EBF");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::LineDashed), L"\x865A\x7EBF");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::LineDotted), L"\x70B9\x7EBF");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Undo), L"\x64A4\x9500");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Redo), L"\x91CD\x505A");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Confirm), L"\x5B8C\x6210");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Cancel), L"\x53D6\x6D88");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Move), L"\x79FB\x52A8");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Eyedropper),
               L"\x53D6\x8272\x5668");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::LongShot), L"\x957F\x622A\x56FE");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Pause), L"\x6682\x505C");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Resume), L"\x7EE7\x7EED");
  EXPECT_STREQ(toolbarIconLabel(ToolbarIconKind::Stop), L"\x505C\x6B62");
}

TEST(ModernToolbarTest, StrokePresetLabelsAreChinese)
{
  EXPECT_STREQ(toolbarStrokePresetLabel(0), L"\x7EC6");
  EXPECT_STREQ(toolbarStrokePresetLabel(1), L"\x4E2D");
  EXPECT_STREQ(toolbarStrokePresetLabel(2), L"\x7C97");
  EXPECT_STREQ(toolbarStrokePresetLabel(-1), L"");
  EXPECT_STREQ(toolbarStrokePresetLabel(3), L"");
}

TEST(ModernToolbarTest, ColorPresetLabelsAreChinese)
{
  EXPECT_STREQ(toolbarColorPresetLabel(0), L"\x7EA2");
  EXPECT_STREQ(toolbarColorPresetLabel(1), L"\x6A59");
  EXPECT_STREQ(toolbarColorPresetLabel(2), L"\x9EC4");
  EXPECT_STREQ(toolbarColorPresetLabel(3), L"\x7EFF");
  EXPECT_STREQ(toolbarColorPresetLabel(4), L"\x9752");
  EXPECT_STREQ(toolbarColorPresetLabel(5), L"\x84DD");
  EXPECT_STREQ(toolbarColorPresetLabel(6), L"\x7D2B");
  EXPECT_STREQ(toolbarColorPresetLabel(7), L"\x767D");
  EXPECT_STREQ(toolbarColorPresetLabel(-1), L"");
  EXPECT_STREQ(toolbarColorPresetLabel(8), L"");
}

TEST(ModernToolbarTest, CornerRadiusMakesStadiumOnDefaultBarHeight)
{
  EXPECT_EQ(DefaultModernToolbarMetrics.corner_radius * 2,
            modernToolbarHeight(DefaultModernToolbarMetrics));
}

TEST(ModernToolbarTest, EyedropperIconFillsDropperBodyFromSvg)
{
  constexpr int kSize = 28;
  constexpr int kCellMid = 14;
  constexpr int kMinInkPixels = 80;
  constexpr int kInkLumaDelta = 120;
  constexpr int kHandleMaxLuma = 400;
  constexpr int kCavityLumaGap = 80;
  constexpr int kHandleX = 21;
  constexpr int kHandleY = 8;
  constexpr int kCavityX = 12;
  constexpr int kCavityY = 16;
  constexpr COLORREF kInk = RGB(55, 59, 66);
  constexpr int kWhiteLuma = 255 * 3;
  void* bits = nullptr;
  const HBITMAP dib = createTopDownArgbDib(kSize, kSize, &bits);
  ASSERT_NE(dib, nullptr);
  ASSERT_NE(bits, nullptr);

  HDC dc = CreateCompatibleDC(nullptr);
  ASSERT_NE(dc, nullptr);
  const HGDIOBJ old = SelectObject(dc, dib);
  RECT cell{0, 0, kSize, kSize};
  const HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
  ASSERT_NE(white, nullptr);
  FillRect(dc, &cell, white);
  DeleteObject(white);
  drawToolbarIcon(dc, cell, ToolbarIconKind::Eyedropper, kInk);
  SelectObject(dc, old);
  DeleteDC(dc);

  const std::uint32_t* pixels = static_cast<const std::uint32_t*>(bits);
  int ink_pixels = 0;
  int upper_right = 0;
  int lower_left = 0;
  int upper_left = 0;
  int lower_right = 0;
  for (int y = 0; y < kSize; ++y)
  {
    for (int x = 0; x < kSize; ++x)
    {
      const std::uint32_t packed =
          pixels[static_cast<std::size_t>(y) * kSize +
                  static_cast<std::size_t>(x)];
      const int luma = static_cast<int>(packed & 0xFFu) +
                       static_cast<int>((packed >> 8) & 0xFFu) +
                       static_cast<int>((packed >> 16) & 0xFFu);
      if (kWhiteLuma - luma < kInkLumaDelta)
      {
        continue;
      }
      ++ink_pixels;
      if (x >= kCellMid && y < kCellMid)
      {
        ++upper_right;
      }
      else if (x < kCellMid && y >= kCellMid)
      {
        ++lower_left;
      }
      else if (x < kCellMid && y < kCellMid)
      {
        ++upper_left;
      }
      else
      {
        ++lower_right;
      }
    }
  }

  const std::uint32_t handle =
      pixels[kHandleY * kSize + kHandleX];
  const std::uint32_t cavity =
      pixels[kCavityY * kSize + kCavityX];
  const int handle_luma = static_cast<int>(handle & 0xFFu) +
                          static_cast<int>((handle >> 8) & 0xFFu) +
                          static_cast<int>((handle >> 16) & 0xFFu);
  const int cavity_luma = static_cast<int>(cavity & 0xFFu) +
                           static_cast<int>((cavity >> 8) & 0xFFu) +
                           static_cast<int>((cavity >> 16) & 0xFFu);

  DeleteObject(dib);

  EXPECT_GE(ink_pixels, kMinInkPixels);
  EXPECT_GT(upper_right + lower_left, upper_left + lower_right);
  EXPECT_LT(handle_luma, kHandleMaxLuma);
  EXPECT_GT(cavity_luma, handle_luma + kCavityLumaGap);
}

}  // namespace qingying
