#include "browser_shell_atlas.hpp"

#include <algorithm>
#include <limits>

namespace qingying {
namespace {

constexpr std::uint8_t MaximumCellMisses = 3;
constexpr std::uint8_t MaximumEntryValidationFailures = 2;
constexpr int LogicalCellSize = 16;
constexpr int DefaultDpi = 96;
constexpr int MaximumIdentityJitterPixels = 2;

bool contains(const WindowRect& rect, POINT point) noexcept
{
  return !rect.empty() && point.x >= rect.left && point.x < rect.right &&
         point.y >= rect.top && point.y < rect.bottom;
}

std::int64_t area(const WindowRect& rect) noexcept
{
  if (rect.empty())
  {
    return 0;
  }
  return static_cast<std::int64_t>(rect.width()) * rect.height();
}

bool overlaps(const WindowRect& left, const WindowRect& right) noexcept
{
  return left.left < right.right && right.left < left.right &&
         left.top < right.bottom && right.top < left.bottom;
}

bool hasOnlySmallEdgeChange(const WindowRect& left,
                            const WindowRect& right) noexcept
{
  return std::abs(left.left - right.left) <= MaximumIdentityJitterPixels &&
         std::abs(left.top - right.top) <= MaximumIdentityJitterPixels &&
         std::abs(left.right - right.right) <= MaximumIdentityJitterPixels &&
         std::abs(left.bottom - right.bottom) <= MaximumIdentityJitterPixels;
}

bool hasSameIdentity(const BrowserShellEntry& left,
                     const BrowserShellEntry& right) noexcept
{
  return left.m_identity_hash == right.m_identity_hash &&
         left.m_role == right.m_role && left.m_row_id == right.m_row_id &&
         (overlaps(left.m_hit_rect, right.m_hit_rect) ||
          hasOnlySmallEdgeChange(left.m_hit_rect, right.m_hit_rect));
}

int sourcePriority(BrowserShellSource source) noexcept
{
  switch (source)
  {
    case BrowserShellSource::UiaSemantic:
      return 4;
    case BrowserShellSource::UiaFast:
      return 3;
    case BrowserShellSource::Visual:
      return 2;
    case BrowserShellSource::Geometry:
      return 1;
  }
  return 0;
}

bool isInteractiveRole(BrowserShellRole role) noexcept
{
  switch (role)
  {
    case BrowserShellRole::Unknown:
    case BrowserShellRole::Pane:
      return false;
    case BrowserShellRole::Button:
    case BrowserShellRole::Tab:
    case BrowserShellRole::TabCloseButton:
    case BrowserShellRole::NewTabButton:
    case BrowserShellRole::BackButton:
    case BrowserShellRole::ForwardButton:
    case BrowserShellRole::ReloadButton:
    case BrowserShellRole::AddressBar:
    case BrowserShellRole::ExtensionButton:
    case BrowserShellRole::ProfileButton:
    case BrowserShellRole::MenuButton:
    case BrowserShellRole::Bookmark:
    case BrowserShellRole::BookmarkFolder:
    case BrowserShellRole::ExtensionAction:
    case BrowserShellRole::ToolbarIcon:
      return true;
  }
  return false;
}

bool isPreferred(const BrowserShellEntry& left,
                 const BrowserShellEntry& right) noexcept
{
  const int left_source_priority = sourcePriority(left.m_source);
  const int right_source_priority = sourcePriority(right.m_source);
  if (left_source_priority != right_source_priority)
  {
    return left_source_priority > right_source_priority;
  }
  const bool left_interactive = isInteractiveRole(left.m_role);
  const bool right_interactive = isInteractiveRole(right.m_role);
  if (left_interactive != right_interactive)
  {
    return left_interactive;
  }
  if (left.m_confidence != right.m_confidence)
  {
    return left.m_confidence > right.m_confidence;
  }
  const std::int64_t left_area = area(left.m_hit_rect);
  const std::int64_t right_area = area(right.m_hit_rect);
  if (left_area != right_area)
  {
    return left_area < right_area;
  }
  if (left.m_updated_at_ms != right.m_updated_at_ms)
  {
    return left.m_updated_at_ms > right.m_updated_at_ms;
  }
  if (left.m_identity_hash != right.m_identity_hash)
  {
    return left.m_identity_hash < right.m_identity_hash;
  }
  if (left.m_row_id != right.m_row_id)
  {
    return left.m_row_id < right.m_row_id;
  }
  if (left.m_hit_rect.left != right.m_hit_rect.left)
  {
    return left.m_hit_rect.left < right.m_hit_rect.left;
  }
  if (left.m_hit_rect.top != right.m_hit_rect.top)
  {
    return left.m_hit_rect.top < right.m_hit_rect.top;
  }
  if (left.m_hit_rect.right != right.m_hit_rect.right)
  {
    return left.m_hit_rect.right < right.m_hit_rect.right;
  }
  return left.m_hit_rect.bottom < right.m_hit_rect.bottom;
}

bool hasSameContext(const BrowserShellContext& left,
                    const BrowserShellContext& right) noexcept
{
  return left.m_root_window == right.m_root_window &&
         left.m_process_id == right.m_process_id &&
         left.m_window_rect.left == right.m_window_rect.left &&
         left.m_window_rect.top == right.m_window_rect.top &&
         left.m_window_rect.right == right.m_window_rect.right &&
         left.m_window_rect.bottom == right.m_window_rect.bottom &&
         left.m_dpi == right.m_dpi &&
         left.m_capture_session_generation ==
             right.m_capture_session_generation &&
         left.m_window_generation == right.m_window_generation &&
         left.m_full_screen == right.m_full_screen;
}

int cellCoordinate(int coordinate, UINT dpi) noexcept
{
  const UINT safe_dpi = dpi == 0 ? DefaultDpi : dpi;
  const int cell_size = (LogicalCellSize * static_cast<int>(safe_dpi) +
                         DefaultDpi - 1) /
                        DefaultDpi;
  const std::int64_t safe_cell_size = (std::max)(cell_size, 1);
  const std::int64_t value = coordinate;
  if (value >= 0)
  {
    return static_cast<int>(value / safe_cell_size);
  }
  return static_cast<int>(
      -(((-value) + safe_cell_size - 1) / safe_cell_size));
}

}  // namespace

bool BrowserShellAtlas::hitTest(POINT screen_point,
                                BrowserShellEntry& out) const noexcept
{
  out = BrowserShellEntry{};
  if (!m_has_context)
  {
    return false;
  }
  bool found = false;
  for (std::size_t index = 0; index < m_entry_count; ++index)
  {
    const BrowserShellEntry& entry = m_entries.at(index);
    if (!contains(entry.m_hit_rect, screen_point))
    {
      continue;
    }
    if (!found || isPreferred(entry, out))
    {
      out = entry;
      found = true;
    }
  }
  return found;
}

bool BrowserShellAtlas::hitTestSnapshot(
    const BrowserShellAtlasSnapshot& snapshot,
    const BrowserShellContext& context,
    POINT screen_point,
    BrowserShellEntry& out) noexcept
{
  out = BrowserShellEntry{};
  if (!hasSameContext(snapshot.m_context, context))
  {
    return false;
  }

  bool found = false;
  const std::size_t entry_count =
      (std::min)(snapshot.m_entry_count, snapshot.m_entries.size());
  for (std::size_t index = 0; index < entry_count; ++index)
  {
    const BrowserShellEntry& entry = snapshot.m_entries.at(index);
    if (!contains(entry.m_hit_rect, screen_point))
    {
      continue;
    }
    if (!found || isPreferred(entry, out))
    {
      out = entry;
      found = true;
    }
  }
  return found;
}

bool BrowserShellAtlas::merge(const BrowserShellEntryCollection& entries,
                               std::uint64_t now_ms) noexcept
{
  if (!m_has_context || entries.empty())
  {
    return false;
  }
  bool changed = false;
  for (std::size_t index = 0; index < entries.count(); ++index)
  {
    BrowserShellEntry incoming = entries.entryAt(index);
    if (incoming.m_hit_rect.empty())
    {
      continue;
    }
    if (incoming.m_updated_at_ms == 0)
    {
      incoming.m_updated_at_ms = now_ms;
    }
    const std::size_t matching_index = findMatchingEntry(incoming);
    if (matching_index < m_entry_count)
    {
      BrowserShellEntry& existing = m_entries.at(matching_index);
      m_entry_validation_failures.at(matching_index) = 0;
      if (hasOnlySmallEdgeChange(existing.m_hit_rect, incoming.m_hit_rect) &&
          sourcePriority(incoming.m_source) <= sourcePriority(existing.m_source))
      {
        continue;
      }
      if (isPreferred(incoming, existing))
      {
        existing = incoming;
        changed = true;
      }
      continue;
    }
    if (m_entry_count < MaximumEntries)
    {
      m_entries.at(m_entry_count) = incoming;
      m_entry_validation_failures.at(m_entry_count) = 0;
      ++m_entry_count;
      changed = true;
      continue;
    }
    const std::size_t worst_index = findWorstEntry();
    if (isPreferred(incoming, m_entries.at(worst_index)))
    {
      m_entries.at(worst_index) = incoming;
      m_entry_validation_failures.at(worst_index) = 0;
      changed = true;
    }
  }
  if (changed)
  {
    sortEntries();
  }
  return changed;
}

void BrowserShellAtlas::reset(const BrowserShellContext& context) noexcept
{
  m_context = context;
  m_layout_generation = 0;
  m_has_context = true;
  m_has_miss_cell = false;
  m_consecutive_cell_misses = 0;
  clearEntries();
}

void BrowserShellAtlas::invalidate() noexcept
{
  m_has_context = false;
  m_has_miss_cell = false;
  m_consecutive_cell_misses = 0;
  clearEntries();
}

bool BrowserShellAtlas::matches(const BrowserShellContext& context) const noexcept
{
  return m_has_context && hasSameContext(m_context, context);
}

void BrowserShellAtlas::advanceLayoutGeneration() noexcept
{
  if (!m_has_context)
  {
    return;
  }
  ++m_layout_generation;
  m_has_miss_cell = false;
  m_consecutive_cell_misses = 0;
  clearEntries();
}

void BrowserShellAtlas::recordCellMiss(POINT screen_point) noexcept
{
  if (!m_has_context)
  {
    return;
  }
  const int cell_x = cellCoordinate(screen_point.x, m_context.m_dpi);
  const int cell_y = cellCoordinate(screen_point.y, m_context.m_dpi);
  if (!m_has_miss_cell || cell_x != m_miss_cell_x || cell_y != m_miss_cell_y)
  {
    m_miss_cell_x = cell_x;
    m_miss_cell_y = cell_y;
    m_consecutive_cell_misses = 0;
    m_has_miss_cell = true;
  }
  ++m_consecutive_cell_misses;
  if (m_consecutive_cell_misses >= MaximumCellMisses)
  {
    advanceLayoutGeneration();
  }
}

void BrowserShellAtlas::recordEntryValidation(const BrowserShellEntry& entry,
                                               bool succeeded) noexcept
{
  const std::size_t index = findMatchingEntry(entry);
  if (index >= m_entry_count)
  {
    return;
  }
  if (succeeded)
  {
    m_entry_validation_failures.at(index) = 0;
    return;
  }
  ++m_entry_validation_failures.at(index);
  if (m_entry_validation_failures.at(index) >=
      MaximumEntryValidationFailures)
  {
    removeAt(index);
  }
}

std::shared_ptr<const BrowserShellAtlasSnapshot>
BrowserShellAtlas::makeSnapshot() const
{
  std::shared_ptr<BrowserShellAtlasSnapshot> snapshot =
      std::make_shared<BrowserShellAtlasSnapshot>();
  snapshot->m_context = m_context;
  snapshot->m_entries = m_entries;
  snapshot->m_entry_count = m_entry_count;
  snapshot->m_layout_generation = m_layout_generation;
  return snapshot;
}

void BrowserShellAtlas::clearEntries() noexcept
{
  m_entries = {};
  m_entry_validation_failures = {};
  m_entry_count = 0;
}

void BrowserShellAtlas::removeAt(std::size_t index) noexcept
{
  if (index >= m_entry_count)
  {
    return;
  }
  for (std::size_t next_index = index + 1; next_index < m_entry_count;
       ++next_index)
  {
    m_entries.at(next_index - 1) = m_entries.at(next_index);
    m_entry_validation_failures.at(next_index - 1) =
        m_entry_validation_failures.at(next_index);
  }
  --m_entry_count;
  m_entries.at(m_entry_count) = BrowserShellEntry{};
  m_entry_validation_failures.at(m_entry_count) = 0;
}

std::size_t BrowserShellAtlas::findMatchingEntry(
    const BrowserShellEntry& entry) const noexcept
{
  for (std::size_t index = 0; index < m_entry_count; ++index)
  {
    if (hasSameIdentity(m_entries.at(index), entry))
    {
      return index;
    }
  }
  return m_entry_count;
}

std::size_t BrowserShellAtlas::findWorstEntry() const noexcept
{
  std::size_t worst_index = 0;
  for (std::size_t index = 1; index < m_entry_count; ++index)
  {
    if (isPreferred(m_entries.at(worst_index), m_entries.at(index)))
    {
      worst_index = index;
    }
  }
  return worst_index;
}

void BrowserShellAtlas::sortEntries() noexcept
{
  for (std::size_t index = 1; index < m_entry_count; ++index)
  {
    std::size_t current_index = index;
    while (current_index > 0 &&
           isPreferred(m_entries.at(current_index),
                       m_entries.at(current_index - 1)))
    {
      std::swap(m_entries.at(current_index), m_entries.at(current_index - 1));
      std::swap(m_entry_validation_failures.at(current_index),
                m_entry_validation_failures.at(current_index - 1));
      --current_index;
    }
  }
}

}  // namespace qingying
