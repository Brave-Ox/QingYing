#include <algorithm>

#include "qingying/overlay/selection_controller.hpp"

namespace qingying {

void SelectionController::begin(int start_x, int start_y) {
  m_start_x = start_x;
  m_start_y = start_y;
  // 重置为「无有效选区」（cancelled==true，宽高 0），直到 update() 拖出矩形。
  m_selection = SelectionResult{};
}

void SelectionController::update(int current_x, int current_y) {
  // 拖出矩形 → 进入「进行中」，选区实时可见（UI 据此绘制选框）。
  m_selection.cancelled = false;
  // 起点始终是矩形的左上角，拖动终点任意方向都归一化到非负宽高。
  const int left = std::min(m_start_x, current_x);
  const int top = std::min(m_start_y, current_y);
  const int right = std::max(m_start_x, current_x);
  const int bottom = std::max(m_start_y, current_y);

  m_selection.x = left;
  m_selection.y = top;
  m_selection.width = right - left;
  m_selection.height = bottom - top;
}

void SelectionController::confirm() {
  // 面积为 0（纯点击未拖动）视为取消，与「按下 Esc」语义一致。
  if (m_selection.width == 0 && m_selection.height == 0) {
    m_selection.cancelled = true;
  }
}

void SelectionController::cancel() {
  m_selection = SelectionResult{};  // 默认即 cancelled==true，宽高归零
}

const SelectionResult& SelectionController::selection() const {
  return m_selection;
}

}  // namespace qingying
