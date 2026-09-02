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

// 框选操作条 / 标注底栏共用：白色圆角条 + 深色线标（PixPin 风格）。
// 空闲态不铺深灰方钮；悬停/选中只用浅灰底。条上说明文字用 label。
enum class ToolbarIconKind
{
  Copy,
  Save,
  Edit,
  Pin,
  Rectangle,
  Ellipse,
  Geometry,
  Fill,
  LineSolid,
  LineDashed,
  LineDotted,
  Arrow,
  Pen,
  Mosaic,
  Text,
  StrokeWidth,
  Undo,
  Confirm,
  Cancel,
  Move,
  Eyedropper,
  LongShot,
  Pause,
  Resume,
  Stop,
};

inline constexpr int kToolbarTooltipMaxChars = 16;

struct ModernToolbarMetrics
{
  int item_size{28};
  int gap{4};
  int bar_padding{8};
  int corner_radius{22};
  int hover_radius{6};
  int divider_gap{10};
  int divider_width{1};
  int font_combo_width{52};
  int tooltip_delay_ms{400};
};

inline constexpr ModernToolbarMetrics DefaultModernToolbarMetrics{};

struct ModernToolbarColors
{
  COLORREF bar_fill{RGB(255, 255, 255)};
  COLORREF bar_border{RGB(226, 229, 234)};
  COLORREF bar_shadow{RGB(214, 218, 224)};
  COLORREF button_fill{RGB(255, 255, 255)};
  COLORREF hover_fill{RGB(245, 247, 249)};
  COLORREF selected_fill{RGB(236, 239, 243)};
  COLORREF disabled_fill{RGB(244, 245, 247)};
  COLORREF icon{RGB(55, 59, 66)};
  COLORREF icon_disabled{RGB(176, 180, 186)};
  COLORREF label{RGB(55, 59, 66)};
  COLORREF divider{RGB(226, 229, 234)};
  COLORREF confirm{RGB(46, 167, 90)};
  COLORREF cancel{RGB(72, 76, 84)};
};

inline constexpr ModernToolbarColors DefaultModernToolbarColors{};

// 工具栏窗口圆角外的色键，避免 SetWindowRgn 与 GDI RoundRect 错位产生毛刺。
inline constexpr COLORREF kToolbarColorKey{RGB(255, 0, 255)};

void fillRoundRect(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border,
                   int radius);

HBITMAP createTopDownArgbDib(int width, int height, void** bits);
void applyColorKeyAlpha(void* bits, int width, int height, COLORREF key);
void promoteRgbToOpaqueAlpha(void* bits, int width, int height);
bool presentLayeredArgbWindow(HWND hwnd, HDC src_dc, int width, int height);
bool drawToolbarBarOnArgbBits(void* bits, int width, int height,
                              const RECT& rect);

void fillToolbarColorKey(HDC hdc, const RECT& rect);
void applyToolbarColorKey(HWND hwnd);

void drawToolbarBar(HDC hdc, const RECT& rect);

void drawToolbarIcon(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     COLORREF color);

void drawToolbarItem(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     bool hovered, bool selected, bool enabled, bool accent,
                     bool grouped = false);

void drawToolbarDivider(HDC hdc, int x, int top, int bottom);

const wchar_t* toolbarIconLabel(ToolbarIconKind kind);
const wchar_t* toolbarStrokePresetLabel(int index);
const wchar_t* toolbarColorPresetLabel(int index);

HWND createToolbarTooltip(HWND owner);
void bindToolbarTooltip(HWND tooltip, HWND owner, UINT id, const RECT& rect,
                        const wchar_t* text, wchar_t* storage,
                        std::size_t storage_chars);

int modernToolbarHeight(const ModernToolbarMetrics& metrics);
int modernToolbarWidth(int item_count, int extra_width,
                       const ModernToolbarMetrics& metrics);

}  // namespace qingying
