#pragma once

#include "qingying/overlay/selection_overlay.hpp"

namespace qingying {

// 纯逻辑选区状态机：与平台（Win32/GDI）完全解耦，可单元测试。
// 全屏遮罩 UI 壳（SelectionOverlay）只负责把鼠标事件喂进来，
// 框选计算、矩形归一化、取消判定都由本类完成，UI 保持薄壳。
//
// 生命周期：begin() → update()* → confirm() | cancel()
// 不变量：confirm() 之后 cancelled == (width == 0 && height == 0)
class SelectionController {
 public:
  // 开始一次框选：记录起点（鼠标按下），并重置上一次的选区结果。
  void begin(int start_x, int start_y);

  // 拖动中更新终点（鼠标移动）；矩形始终归一化，width/height 恒非负。
  void update(int current_x, int current_y);

  // 确认选区（鼠标松开）；若面积为 0（纯点击未拖动）则视为取消。
  void confirm();

  // 取消本次框选（如按下 Esc）。
  void cancel();

  // 当前选区：update() 期间随拖动实时变化，confirm()/cancel() 后为最终值。
  const SelectionResult& selection() const;

 private:
  SelectionResult m_selection;
  int m_start_x{0};
  int m_start_y{0};
};

}  // namespace qingying
