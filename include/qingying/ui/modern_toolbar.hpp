#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <Windows.h>

#include <cstddef>

namespace qingying {

// 框选操作条 / 标注底栏共用：浅灰圆角条 + 深灰方钮 + 白线图标。
enum class ToolbarIconKind
{
  Copy,
  Save,
  Edit,
  Pin,
  Rectangle,
  Ellipse,
  Arrow,
  Pen,
  Text,
  Undo,
  Confirm,
  Cancel,
};

inline constexpr int kToolbarTooltipMaxChars = 16;

struct ModernToolbarMetrics
{
  int item_size{28};
  int gap{4};
  int bar_padding{8};
  int corner_radius{16};
  int hover_radius{6};
  int divider_gap{10};
  int divider_width{1};
  int font_combo_width{52};
  int tooltip_delay_ms{400};
};

inline constexpr ModernToolbarMetrics DefaultModernToolbarMetrics{};

struct ModernToolbarColors
{
  COLORREF bar_fill{RGB(236, 238, 241)};
  COLORREF bar_border{RGB(220, 223, 228)};
  COLORREF bar_shadow{RGB(206, 210, 216)};
  COLORREF button_fill{RGB(92, 98, 108)};
  COLORREF hover_fill{RGB(52, 56, 64)};
  COLORREF selected_fill{RGB(52, 56, 64)};
  COLORREF disabled_fill{RGB(186, 190, 196)};
  COLORREF icon{RGB(255, 255, 255)};
  COLORREF icon_disabled{RGB(232, 234, 237)};
  COLORREF divider{RGB(210, 214, 219)};
};

inline constexpr ModernToolbarColors DefaultModernToolbarColors{};

void fillRoundRect(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border,
                   int radius);

void drawToolbarBar(HDC hdc, const RECT& rect);

void drawToolbarIcon(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     COLORREF color);

void drawToolbarItem(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     bool hovered, bool selected, bool enabled, bool accent);

void drawToolbarDivider(HDC hdc, int x, int top, int bottom);

const wchar_t* toolbarIconLabel(ToolbarIconKind kind);

HWND createToolbarTooltip(HWND owner);
void bindToolbarTooltip(HWND tooltip, HWND owner, UINT id, const RECT& rect,
                        const wchar_t* text, wchar_t* storage,
                        std::size_t storage_chars);

int modernToolbarHeight(const ModernToolbarMetrics& metrics);
int modernToolbarWidth(int item_count, int extra_width,
                       const ModernToolbarMetrics& metrics);

}  // namespace qingying
