#pragma once

#include "qingying/overlay/selection_handles.hpp"
#include "qingying/overlay/selection_overlay.hpp"

namespace qingying {

// 纯逻辑选区状态机：与平台（Win32/GDI）完全解耦，可单元测试。
// 全屏遮罩 UI 壳（SelectionOverlay）只负责把鼠标事件喂进来，
// 框选计算、矩形归一化、八点调整、整体移动、边界/最小尺寸钳制、
// 命中测试都由本类完成，UI 保持薄壳。
//
// 生命周期：
//   创建：      begin() → update()* → confirm() | cancel()
//   调整/移动： beginResize()/beginMove() → updateResize()/updateMove()* → endDrag()
//
// 不变量：
//   - width/height 恒 >= 0；确认后 >= 1；
//   - confirm() 之后 cancelled == (width <= 0 || height <= 0)；
//   - 反向拖拽始终归一化为左上角 + 非负宽高；
//   - 选区被钳制在 setBounds() 给出的有效桌面范围内。
class SelectionController {
 public:
  // --- 创建选区 ---
  // 开始一次框选：记录起点（鼠标按下），并重置上一次的选区结果。
  void begin(int start_x, int start_y);

  // 拖动中更新终点（鼠标移动）；矩形始终归一化，且被钳制在有效范围内。
  void update(int current_x, int current_y);

  // 确认选区（鼠标松开）；宽度或高度 <= 0（纯点击未拖动）视为取消。
  void confirm();

  // --- 调整 / 移动（确认之后） ---
  // 从某个手柄开始调整大小；记录拖拽起点与锚点选区。
  void beginResize(SelectionHandle handle, int x, int y);
  // 拖动中按手柄语义调整大小（固定边不动，最小尺寸/边界钳制）。
  void updateResize(int x, int y);

  // 开始整体移动；记录拖拽起点与锚点选区。
  void beginMove(int x, int y);
  // 拖动中整体平移选区（边界钳制）。
  void updateMove(int x, int y);

  // 结束一次调整/移动（鼠标松开）。
  void endDrag();

  // --- 通用 ---
  // 取消本次操作（如按下 Esc / 右键）；选区回到无有效选区状态。
  void cancel();

  // 当前选区：拖动期间实时变化，confirm()/cancel()/endDrag() 后为最终值。
  const SelectionResult& selection() const;

  // 设置有效桌面边界（覆盖层客户区坐标，原点 0,0）；选区会钳制在此范围内。
  void setBounds(int width, int height);

  // 命中测试：基于当前已确认选区，返回点 (x, y) 上的手柄 / 内部 / 外部。
  SelectionHandle hitTest(int x, int y) const;

  // 设置手柄命中半径（逻辑像素）；覆盖层按 DPI 缩放后传入。
  void setHandleRadius(int radius);

  // 直接写入已确认的吸附选区（窗口吸附），不经过鼠标拖拽；宽高 <= 0 则取消。
  void setSelection(int x, int y, int width, int height);

 private:
  enum class Mode { None, Creating, Resizing, Moving };

  bool hasBounds() const { return m_bounds_width > 0 && m_bounds_height > 0; }
  // 把当前选区钳制到 [0,bounds_w]×[0,bounds_h]（保持宽高不变）。
  void clampToBounds();

  SelectionResult m_selection;
  Mode m_mode{Mode::None};
  int m_start_x{0};
  int m_start_y{0};
  int m_drag_x{0};
  int m_drag_y{0};
  SelectionHandle m_handle{SelectionHandle::None};
  SelectionResult m_anchor;
  int m_bounds_width{0};
  int m_bounds_height{0};
  int m_handle_radius{handles::kHandleHitRadius};
};

}  // namespace qingying
