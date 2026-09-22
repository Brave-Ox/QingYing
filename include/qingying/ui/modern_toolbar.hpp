#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <Windows.h>

#include <cstddef>

#include "qingying/ui/toolbar_model.h"

namespace qingying {

// Win32/GDI 与 layered-window 绘制后端。纯布局和图标状态见 toolbar_model.h。
// 框选操作条 / 标注底栏共用：白色圆角条 + 深色线标（PixPin 风格）。
// 空闲态不铺深灰方钮；悬停/选中只用浅灰底。条上说明文字用 label。
struct ModernToolbarColors
{
  COLORREF bar_fill{RGB(255, 255, 255)};
  COLORREF bar_border{RGB(226, 229, 234)};
  COLORREF bar_shadow{RGB(214, 218, 224)};
  COLORREF button_fill{RGB(248, 250, 252)};
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

// 在进入交互路径前初始化 GDI+。托盘应用启动时预热，避免首次框选完成后
// 才支付图标后端初始化成本。
bool prepareModernToolbarRendering() noexcept;

void fillRoundRect(HDC hdc, const RECT& rect, COLORREF fill, COLORREF border,
                   int radius);

HBITMAP createTopDownArgbDib(int width, int height, void** bits);
void applyColorKeyAlpha(void* bits, int width, int height, COLORREF key);
void promoteRgbToOpaqueAlpha(void* bits, int width, int height);
bool presentLayeredArgbWindow(HWND hwnd, HDC src_dc, int width, int height);
bool drawToolbarBarOnArgbBits(void* bits, int width, int height,
                              const RECT& rect);
bool drawToolbarBarOnArgbBits(void* bits, int width, int height,
                              const RECT& rect,
                              const ModernToolbarMetrics& metrics);

void fillToolbarColorKey(HDC hdc, const RECT& rect);
void applyToolbarColorKey(HWND hwnd);

void drawToolbarBar(HDC hdc, const RECT& rect);
void drawToolbarBar(HDC hdc, const RECT& rect,
                    const ModernToolbarMetrics& metrics);

void drawToolbarIcon(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     COLORREF color);

void drawToolbarItem(HDC hdc, const RECT& cell, ToolbarIconKind kind,
                     bool hovered, bool selected, bool enabled, bool accent,
                     bool grouped = false);

// GDI 绘制后端的 Model 入口；上面的布尔参数保留旧调用兼容性。
void drawToolbarItem(HDC hdc, const RECT& cell, const ToolbarItemModel& model);

void drawToolbarItem(HDC hdc, const RECT& cell, const ToolbarItemModel& model,
                     const ModernToolbarMetrics& metrics);

void drawToolbarDivider(HDC hdc, int x, int top, int bottom);

void drawToolbarHorizontalDivider(HDC hdc, int left, int right, int y);

HWND createToolbarTooltip(HWND owner);
void bindToolbarTooltip(HWND tooltip, HWND owner, UINT id, const RECT& rect,
                        const wchar_t* text, wchar_t* storage,
                        std::size_t storage_chars);

}  // namespace qingying
