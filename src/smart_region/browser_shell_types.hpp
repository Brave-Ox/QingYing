#pragma once

#include <cstdint>

namespace qingying {

// Describes policy input only and does not participate in candidate selection.
enum class BrowserShellRole : std::uint8_t
{
  Unknown,
  Button,
  BookmarkFolder,
  ExtensionAction,
  ToolbarIcon,
  Pane,
};

}  // namespace qingying
