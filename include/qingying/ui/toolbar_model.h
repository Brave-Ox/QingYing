#pragma once

#include <cstddef>

namespace qingying {

// Win32 绘制后端只消费这些稳定的图标语义，不参与业务状态推导。
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
  Redo,
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

// Toolbar Model：与 HWND、HDC 和分层窗口合成无关的单项显示状态。
struct ToolbarItemModel
{
  ToolbarIconKind icon{ToolbarIconKind::Copy};
  bool hovered{false};
  bool selected{false};
  bool enabled{true};
  bool accent{false};
  bool grouped{false};
};

const wchar_t* toolbarIconLabel(ToolbarIconKind kind);
const wchar_t* toolbarStrokePresetLabel(int index);
const wchar_t* toolbarColorPresetLabel(int index);
const wchar_t* toolbarLineStyleLabel(int index);
const wchar_t* toolbarArrowStyleLabel(int index);

int modernToolbarHeight(const ModernToolbarMetrics& metrics);
int modernToolbarWidth(int item_count, int extra_width,
                       const ModernToolbarMetrics& metrics);

}  // namespace qingying
