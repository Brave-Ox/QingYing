#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <Windows.h>

#include "qingying/window/smart_region_types.hpp"

#include "browser_shell_types.hpp"

namespace qingying::window_detail {

// UIA 控件类型的内部、平台无关映射，便于不依赖 UIA 服务的单元测试。
enum class UiaControlType : std::uint8_t {
  Unknown,
  Button,
  CheckBox,
  RadioButton,
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
  Pane,
  Group,
  Custom,
};

enum class UiaPatternFlag : std::uint8_t {
  Invoke = 1U << 0,
  Toggle = 1U << 1,
  SelectionItem = 1U << 2,
  ExpandCollapse = 1U << 3,
  Value = 1U << 4,
  RangeValue = 1U << 5,
  ScrollItem = 1U << 6,
};

struct UiaCacheRequestProfile
{
  bool include_name{false};
  std::uint8_t pattern_flags{0};
};

UiaCacheRequestProfile fastPointCacheRequestProfile() noexcept;

UiaCacheRequestProfile localSemanticCacheRequestProfile(
    UiaControlType control_type) noexcept;

struct UiaRegionProperties {
  WindowRect rect;
  UiaControlType control_type{UiaControlType::Unknown};
  bool is_control{false};
  bool is_content{false};
  bool is_enabled{true};
  bool is_keyboard_focusable{false};
  bool has_name{false};
  HWND native_window{nullptr};
  std::uint32_t control_type_id{0};
  std::uint8_t supported_pattern_flags{0};
};

bool selectSmallestUiaChildAtPoint(
    const UiaRegionProperties* children, std::size_t child_count,
    POINT screen_point, std::size_t& out_index) noexcept;

bool uiaPathBelongsToRoot(const UiaRegionProperties* path,
                          std::size_t path_count,
                          HWND root_window) noexcept;

bool isUsefulRootScopedUiaCandidate(
    const SmartRegionCandidate& candidate,
    const WindowRect& owner_rect) noexcept;

bool makeUiaCandidate(HWND root_window, POINT screen_point,
                      const UiaRegionProperties& properties,
                      SmartRegionCandidate& out) noexcept;

// 将命中元素及其父级的属性转换为有效 UIA 候选，保持原始层级顺序。
std::size_t collectUiaCandidates(
    HWND root_window, POINT screen_point,
    const UiaRegionProperties* properties, std::size_t property_count,
    SmartRegionCandidate* out_candidates, std::size_t capacity) noexcept;

bool collectBrowserShellEntries(
    HWND root_window, POINT screen_point, const WindowRect& target_row,
    const UiaRegionProperties* properties, std::size_t property_count,
    BrowserShellEntryCollection& out_entries) noexcept;

// 一个实例只能由创建它的 COM 线程使用。会话复用 Automation、TreeWalker
// 和 CacheRequest，避免连续鼠标移动期间重复创建跨进程 UIA 对象。
class UiaRegionLocatorSession
{
 public:
  UiaRegionLocatorSession();
  ~UiaRegionLocatorSession();

  UiaRegionLocatorSession(const UiaRegionLocatorSession&) = delete;
  UiaRegionLocatorSession& operator=(const UiaRegionLocatorSession&) = delete;

  bool locate(HWND root_window, POINT screen_point,
              SmartRegionCandidate* out_candidates, std::size_t capacity,
              std::size_t& out_count,
              bool allow_root_scoped_traversal = true) noexcept;
  bool locateBrowserShellEntries(
      HWND root_window, POINT screen_point,
      BrowserShellEntryCollection& out_entries) noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

// 仅查询鼠标下元素和有限父级；UIA 服务不可用或超时时返回 false。
bool locateUiaCandidates(HWND root_window, POINT screen_point,
                         SmartRegionCandidate* out_candidates,
                         std::size_t capacity,
                         std::size_t& out_count) noexcept;

}  // namespace qingying::window_detail
