#include <algorithm>

#include "qingying/overlay/selection_controller.hpp"

namespace qingying {

namespace {

// 最小选区尺寸（逻辑像素）。调整大小时不可小于此值，防止翻转。
constexpr int kMinSelectionSize = 1;

int clampInt(int value, int lo, int hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

}  // namespace

void SelectionController::begin(int start_x, int start_y) {
  m_start_x = start_x;
  m_start_y = start_y;
  // 重置为「无有效选区」（cancelled==true，宽高 0），直到 update() 拖出矩形。
  m_selection = SelectionResult{};
  m_mode = Mode::Creating;
  m_handle = SelectionHandle::None;
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
  clampToBounds();
}

void SelectionController::confirm() {
  // 宽度或高度 <= 0（纯点击未拖动 / 退化选区）视为取消。
  if (m_selection.width <= 0 || m_selection.height <= 0) {
    m_selection.cancelled = true;
  }
  m_mode = Mode::None;
}

void SelectionController::beginResize(SelectionHandle handle, int x, int y) {
  m_handle = handle;
  m_drag_x = x;
  m_drag_y = y;
  m_anchor = m_selection;
  m_mode = Mode::Resizing;
}

void SelectionController::updateResize(int x, int y) {
  if (m_mode != Mode::Resizing) {
    return;
  }
  const int dx = x - m_drag_x;
  const int dy = y - m_drag_y;

  // 用锚点矩形 + 位移计算新边界（right/bottom 为开区间）。
  int new_left = m_anchor.x;
  int new_top = m_anchor.y;
  int new_right = m_anchor.x + m_anchor.width;
  int new_bottom = m_anchor.y + m_anchor.height;

  const bool move_left = (m_handle == SelectionHandle::Left ||
                          m_handle == SelectionHandle::TopLeft ||
                          m_handle == SelectionHandle::BottomLeft);
  const bool move_right = (m_handle == SelectionHandle::Right ||
                           m_handle == SelectionHandle::TopRight ||
                           m_handle == SelectionHandle::BottomRight);
  const bool move_top = (m_handle == SelectionHandle::Top ||
                         m_handle == SelectionHandle::TopLeft ||
                         m_handle == SelectionHandle::TopRight);
  const bool move_bottom = (m_handle == SelectionHandle::Bottom ||
                            m_handle == SelectionHandle::BottomLeft ||
                            m_handle == SelectionHandle::BottomRight);

  if (move_left) {
    new_left += dx;
  }
  if (move_right) {
    new_right += dx;
  }
  if (move_top) {
    new_top += dy;
  }
  if (move_bottom) {
    new_bottom += dy;
  }

  // 最小尺寸：保证固定边不动，只收缩可动边。
  if (new_right - new_left < kMinSelectionSize) {
    if (move_left) {
      new_left = new_right - kMinSelectionSize;
    } else {
      new_right = new_left + kMinSelectionSize;
    }
  }
  if (new_bottom - new_top < kMinSelectionSize) {
    if (move_top) {
      new_top = new_bottom - kMinSelectionSize;
    } else {
      new_bottom = new_top + kMinSelectionSize;
    }
  }

  // 边界钳制：可动边不越出桌面，固定边保持不动。
  if (hasBounds()) {
    if (move_left) {
      new_left = clampInt(new_left, 0, new_right - kMinSelectionSize);
    } else if (move_right) {
      new_right = clampInt(new_right, new_left + kMinSelectionSize,
                           m_bounds_width);
    }
    if (move_top) {
      new_top = clampInt(new_top, 0, new_bottom - kMinSelectionSize);
    } else if (move_bottom) {
      new_bottom = clampInt(new_bottom, new_top + kMinSelectionSize,
                            m_bounds_height);
    }
  }

  m_selection.x = new_left;
  m_selection.y = new_top;
  m_selection.width = new_right - new_left;
  m_selection.height = new_bottom - new_top;
  m_selection.cancelled = false;
}

void SelectionController::beginMove(int x, int y) {
  m_drag_x = x;
  m_drag_y = y;
  m_anchor = m_selection;
  m_mode = Mode::Moving;
}

void SelectionController::updateMove(int x, int y) {
  if (m_mode != Mode::Moving) {
    return;
  }
  m_selection.x = m_anchor.x + (x - m_drag_x);
  m_selection.y = m_anchor.y + (y - m_drag_y);
  clampToBounds();
}

void SelectionController::endDrag() {
  m_mode = Mode::None;
  m_handle = SelectionHandle::None;
}

void SelectionController::cancel() {
  m_selection = SelectionResult{};  // 默认即 cancelled==true，宽高归零
  m_mode = Mode::None;
  m_handle = SelectionHandle::None;
}

const SelectionResult& SelectionController::selection() const {
  return m_selection;
}

void SelectionController::setBounds(int width, int height) {
  m_bounds_width = width > 0 ? width : 0;
  m_bounds_height = height > 0 ? height : 0;
  clampToBounds();
}

SelectionHandle SelectionController::hitTest(int x, int y) const {
  if (m_selection.cancelled || m_selection.width <= 0 ||
      m_selection.height <= 0) {
    return SelectionHandle::None;
  }
  return handles::hitTest(x, y, m_selection.x, m_selection.y,
                          m_selection.width, m_selection.height,
                          m_handle_radius);
}

void SelectionController::setHandleRadius(int radius) {
  m_handle_radius = radius > 0 ? radius : handles::kHandleHitRadius;
}

void SelectionController::clampToBounds() {
  if (!hasBounds() || m_selection.cancelled) {
    return;
  }
  if (m_selection.width > m_bounds_width) {
    m_selection.width = m_bounds_width;
  }
  if (m_selection.height > m_bounds_height) {
    m_selection.height = m_bounds_height;
  }
  if (m_selection.width <= 0 || m_selection.height <= 0) {
    return;
  }
  m_selection.x = clampInt(m_selection.x, 0, m_bounds_width - m_selection.width);
  m_selection.y =
      clampInt(m_selection.y, 0, m_bounds_height - m_selection.height);
}

}  // namespace qingying
