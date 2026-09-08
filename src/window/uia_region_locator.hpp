#pragma once

#include <cstddef>
#include <cstdint>

#include <Windows.h>

#include "qingying/window/smart_region_detector.hpp"

namespace qingying::window_detail {

// UIA 控件类型的内部、平台无关映射，便于不依赖 UIA 服务的单元测试。
enum class UiaControlType : std::uint8_t {
  Unknown,
  Button,
  CheckBox,
  ComboBox,
  DataItem,
  Edit,
  Hyperlink,
  Image,
  ListItem,
  MenuItem,
  TabItem,
  TreeItem,
  Document,
  List,
  Tree,
  DataGrid,
  Table,
};

struct UiaRegionProperties {
  WindowRect rect;
  UiaControlType control_type{UiaControlType::Unknown};
  bool is_control{false};
  bool is_content{false};
};

bool makeUiaCandidate(HWND root_window, POINT screen_point,
                      const UiaRegionProperties& properties,
                      SmartRegionCandidate& out) noexcept;

// 将命中元素及其父级的属性转换为有效 UIA 候选，保持原始层级顺序。
std::size_t collectUiaCandidates(
    HWND root_window, POINT screen_point,
    const UiaRegionProperties* properties, std::size_t property_count,
    SmartRegionCandidate* out_candidates, std::size_t capacity) noexcept;

// 仅查询鼠标下元素和有限父级；UIA 服务不可用或超时时返回 false。
bool locateUiaCandidates(HWND root_window, POINT screen_point,
                         SmartRegionCandidate* out_candidates,
                         std::size_t capacity,
                         std::size_t& out_count) noexcept;

}  // namespace qingying::window_detail
