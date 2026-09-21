#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Windows.h>

#include "qingying/window/smart_region_types.hpp"

namespace qingying {

// Describes policy input only and does not participate in candidate selection.
enum class BrowserShellRole : std::uint8_t
{
  Unknown,
  Button,
  Tab,
  TabCloseButton,
  NewTabButton,
  BackButton,
  ForwardButton,
  ReloadButton,
  AddressBar,
  ExtensionButton,
  ProfileButton,
  MenuButton,
  Bookmark,
  BookmarkFolder,
  ExtensionAction,
  ToolbarIcon,
  Pane,
};

enum class BrowserShellSource : std::uint8_t
{
  Geometry,
  Visual,
  UiaFast,
  UiaSemantic,
};

struct BrowserShellContext
{
  HWND m_root_window{nullptr};
  DWORD m_process_id{0};
  WindowRect m_window_rect;
  UINT m_dpi{96};
  std::uint64_t m_capture_session_generation{0};
  std::uint64_t m_window_generation{0};
  bool m_full_screen{false};
};

struct BrowserShellEntry
{
  WindowRect m_hit_rect;
  BrowserShellRole m_role{BrowserShellRole::Unknown};
  BrowserShellSource m_source{BrowserShellSource::Geometry};
  std::uint8_t m_confidence{0};
  std::uint64_t m_updated_at_ms{0};
  std::uint64_t m_identity_hash{0};
  std::uint32_t m_row_id{0};
};

struct BrowserShellAtlasSnapshot
{
  static constexpr std::size_t MaximumEntries = 64;

  BrowserShellContext m_context;
  std::array<BrowserShellEntry, MaximumEntries> m_entries{};
  std::size_t m_entry_count{0};
  std::uint64_t m_layout_generation{0};
};

class BrowserShellEntryCollection
{
 public:
  static constexpr std::size_t MaximumEntries =
      BrowserShellAtlasSnapshot::MaximumEntries;

  bool append(const BrowserShellEntry& entry) noexcept
  {
    if (entry.m_hit_rect.empty() || m_entry_count >= MaximumEntries)
    {
      return false;
    }
    m_entries.at(m_entry_count) = entry;
    ++m_entry_count;
    return true;
  }

  bool empty() const noexcept
  {
    return m_entry_count == 0;
  }

  std::size_t count() const noexcept
  {
    return m_entry_count;
  }

  const BrowserShellEntry& entryAt(std::size_t index) const noexcept
  {
    return m_entries.at(index);
  }

 private:
  std::array<BrowserShellEntry, MaximumEntries> m_entries{};
  std::size_t m_entry_count{0};
};

}  // namespace qingying
