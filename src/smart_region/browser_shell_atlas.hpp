#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "browser_shell_types.hpp"

namespace qingying {

class BrowserShellAtlas
{
 public:
  static constexpr std::size_t MaximumEntries =
      BrowserShellAtlasSnapshot::MaximumEntries;

  bool hitTest(POINT screen_point, BrowserShellEntry& out) const noexcept;
  bool merge(const BrowserShellEntryCollection& entries,
             std::uint64_t now_ms) noexcept;
  void reset(const BrowserShellContext& context) noexcept;
  void invalidate() noexcept;
  bool matches(const BrowserShellContext& context) const noexcept;
  void advanceLayoutGeneration() noexcept;
  void recordCellMiss(POINT screen_point) noexcept;
  void recordEntryValidation(const BrowserShellEntry& entry,
                             bool succeeded) noexcept;
  std::shared_ptr<const BrowserShellAtlasSnapshot> makeSnapshot() const;

 private:
  void clearEntries() noexcept;
  void removeAt(std::size_t index) noexcept;
  std::size_t findMatchingEntry(const BrowserShellEntry& entry) const noexcept;
  std::size_t findWorstEntry() const noexcept;
  void sortEntries() noexcept;

  BrowserShellContext m_context;
  std::array<BrowserShellEntry, MaximumEntries> m_entries{};
  std::array<std::uint8_t, MaximumEntries> m_entry_validation_failures{};
  std::size_t m_entry_count{0};
  std::uint64_t m_layout_generation{0};
  int m_miss_cell_x{0};
  int m_miss_cell_y{0};
  std::uint8_t m_consecutive_cell_misses{0};
  bool m_has_miss_cell{false};
  bool m_has_context{false};
};

}  // namespace qingying
