#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <functional>
#include <memory>

#include "qingying/overlay/overlay_phase.hpp"
#include "qingying/ui/shortcut_types.hpp"

namespace qingying {

enum class SelectionToolbarCommand {
  Copy,
  Save,
  ToggleLongShot,
  Edit,
  Pin,
  StopLongShot,
  Cancel,
};

inline constexpr int SelectionToolbarCopyHotkeyId = 5;
inline constexpr int SelectionToolbarLongShotHotkeyId = 6;

enum class SelectionToolbarIcon {
  Copy,
  Save,
  LongShot,
  Pause,
  Resume,
  Edit,
  Pin,
  Stop,
};

struct SelectionToolbarItemModel {
  SelectionToolbarCommand command{SelectionToolbarCommand::Copy};
  SelectionToolbarIcon icon{SelectionToolbarIcon::Copy};
  bool enabled{false};
};

inline constexpr std::size_t SelectionToolbarItemCount = 6;
using SelectionToolbarItems =
    std::array<SelectionToolbarItemModel, SelectionToolbarItemCount>;

// 纯状态映射：Win32 工具栏只消费该描述，不自行推导业务状态。
SelectionToolbarItems buildSelectionToolbarItems(OverlayPhase phase) noexcept;

bool selectionToolbarShortcutCommand(
    OverlayPhase phase, const SelectionShortcutSettings& shortcuts,
    const ShortcutBinding& shortcut,
    SelectionToolbarCommand& command) noexcept;

bool selectionToolbarHotkeyCommand(OverlayPhase phase,
                                   const SelectionShortcutSettings& shortcuts,
                                   int hotkey_id,
                                   SelectionToolbarCommand& command) noexcept;

struct SelectionToolbarPlacement {
  int selection_x{0};
  int selection_y{0};
  int selection_width{0};
  int selection_height{0};
  int screen_left{0};
  int screen_top{0};
  int screen_right{0};
  int screen_bottom{0};
};

class SelectionToolbar {
 public:
  using CommandCallback = std::function<void(SelectionToolbarCommand)>;

  SelectionToolbar();
  ~SelectionToolbar();

  SelectionToolbar(const SelectionToolbar&) = delete;
  SelectionToolbar& operator=(const SelectionToolbar&) = delete;

  bool show(HWND owner_window,
            const SelectionToolbarPlacement& placement, OverlayPhase phase,
            CommandCallback callback);
  void update(OverlayPhase phase);
  void hide();
  bool visible() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace qingying
