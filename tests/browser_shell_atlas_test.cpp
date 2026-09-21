#include <cstdint>

#include <gtest/gtest.h>

#include "browser_shell_atlas.hpp"

namespace qingying {
namespace {

BrowserShellContext makeContext()
{
  BrowserShellContext context;
  context.m_root_window =
      reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x1000));
  context.m_process_id = 42;
  context.m_window_rect = {-1920, 0, 1920, 1080};
  context.m_dpi = 96;
  context.m_capture_session_generation = 7;
  context.m_window_generation = 11;
  return context;
}

BrowserShellEntry makeEntry(const WindowRect& rect, BrowserShellRole role,
                            BrowserShellSource source,
                            std::uint64_t identity_hash,
                            std::uint32_t row_id,
                            std::uint64_t updated_at_ms = 0,
                            std::uint8_t confidence = 100)
{
  BrowserShellEntry entry;
  entry.m_hit_rect = rect;
  entry.m_role = role;
  entry.m_source = source;
  entry.m_confidence = confidence;
  entry.m_updated_at_ms = updated_at_ms;
  entry.m_identity_hash = identity_hash;
  entry.m_row_id = row_id;
  return entry;
}

BrowserShellEntryCollection collectionWith(const BrowserShellEntry& entry)
{
  BrowserShellEntryCollection entries;
  EXPECT_TRUE(entries.append(entry));
  return entries;
}

bool containsIdentity(const BrowserShellAtlasSnapshot& snapshot,
                      std::uint64_t identity_hash) noexcept
{
  for (std::size_t index = 0; index < snapshot.m_entry_count; ++index)
  {
    if (snapshot.m_entries.at(index).m_identity_hash == identity_hash)
    {
      return true;
    }
  }
  return false;
}

TEST(BrowserShellAtlasTest, HitsExtensionBookmarkFolderAndTabRoles)
{
  BrowserShellAtlas atlas;
  atlas.reset(makeContext());
  BrowserShellEntryCollection entries;
  ASSERT_TRUE(entries.append(makeEntry(
      {-300, 20, -260, 52}, BrowserShellRole::ExtensionButton,
      BrowserShellSource::UiaSemantic, 1, 1)));
  ASSERT_TRUE(entries.append(makeEntry(
      {-250, 20, -190, 52}, BrowserShellRole::BookmarkFolder,
      BrowserShellSource::UiaSemantic, 2, 2)));
  ASSERT_TRUE(entries.append(makeEntry(
      {-180, 20, -80, 52}, BrowserShellRole::Tab,
      BrowserShellSource::UiaSemantic, 3, 3)));
  ASSERT_TRUE(atlas.merge(entries, 100));

  BrowserShellEntry hit;
  ASSERT_TRUE(atlas.hitTest({-280, 32}, hit));
  EXPECT_EQ(hit.m_role, BrowserShellRole::ExtensionButton);
  ASSERT_TRUE(atlas.hitTest({-220, 32}, hit));
  EXPECT_EQ(hit.m_role, BrowserShellRole::BookmarkFolder);
  ASSERT_TRUE(atlas.hitTest({-100, 32}, hit));
  EXPECT_EQ(hit.m_role, BrowserShellRole::Tab);
}

TEST(BrowserShellAtlasTest, SelectsOverlapBySourceRoleAreaAndUpdateTime)
{
  BrowserShellAtlas atlas;
  atlas.reset(makeContext());
  BrowserShellEntryCollection entries;
  ASSERT_TRUE(entries.append(makeEntry(
      {0, 0, 100, 100}, BrowserShellRole::Button,
      BrowserShellSource::Visual, 10, 1, 20)));
  ASSERT_TRUE(entries.append(makeEntry(
      {0, 0, 100, 100}, BrowserShellRole::Button,
      BrowserShellSource::UiaSemantic, 11, 1, 10)));
  ASSERT_TRUE(entries.append(makeEntry(
      {120, 0, 220, 100}, BrowserShellRole::Unknown,
      BrowserShellSource::UiaSemantic, 12, 1, 40)));
  ASSERT_TRUE(entries.append(makeEntry(
      {120, 0, 220, 100}, BrowserShellRole::Button,
      BrowserShellSource::UiaSemantic, 13, 1, 10)));
  ASSERT_TRUE(entries.append(makeEntry(
      {240, 0, 360, 120}, BrowserShellRole::Button,
      BrowserShellSource::UiaSemantic, 14, 1, 50)));
  ASSERT_TRUE(entries.append(makeEntry(
      {260, 20, 340, 100}, BrowserShellRole::Button,
      BrowserShellSource::UiaSemantic, 15, 1, 10)));
  ASSERT_TRUE(entries.append(makeEntry(
      {380, 0, 460, 80}, BrowserShellRole::Button,
      BrowserShellSource::UiaSemantic, 16, 1, 10)));
  ASSERT_TRUE(entries.append(makeEntry(
      {380, 0, 460, 80}, BrowserShellRole::Button,
      BrowserShellSource::UiaSemantic, 17, 2, 30)));
  ASSERT_TRUE(atlas.merge(entries, 100));

  BrowserShellEntry hit;
  ASSERT_TRUE(atlas.hitTest({50, 50}, hit));
  EXPECT_EQ(hit.m_identity_hash, 11U);
  ASSERT_TRUE(atlas.hitTest({150, 50}, hit));
  EXPECT_EQ(hit.m_identity_hash, 13U);
  ASSERT_TRUE(atlas.hitTest({300, 60}, hit));
  EXPECT_EQ(hit.m_identity_hash, 15U);
  ASSERT_TRUE(atlas.hitTest({400, 40}, hit));
  EXPECT_EQ(hit.m_identity_hash, 17U);
}

TEST(BrowserShellAtlasTest, KeepsSemanticEntryWhenFastOrVisualUpdatesArrive)
{
  BrowserShellAtlas atlas;
  atlas.reset(makeContext());
  const BrowserShellEntry semantic = makeEntry(
      {0, 0, 32, 32}, BrowserShellRole::Button,
      BrowserShellSource::UiaSemantic, 22, 1, 20);
  ASSERT_TRUE(atlas.merge(collectionWith(semantic), 20));

  const BrowserShellEntry fast = makeEntry(
      {0, 0, 32, 32}, BrowserShellRole::Button,
      BrowserShellSource::UiaFast, 22, 1, 30);
  EXPECT_FALSE(atlas.merge(collectionWith(fast), 30));
  const BrowserShellEntry visual = makeEntry(
      {0, 0, 32, 32}, BrowserShellRole::Button,
      BrowserShellSource::Visual, 22, 1, 40);
  EXPECT_FALSE(atlas.merge(collectionWith(visual), 40));

  BrowserShellEntry hit;
  ASSERT_TRUE(atlas.hitTest({16, 16}, hit));
  EXPECT_EQ(hit.m_source, BrowserShellSource::UiaSemantic);
}

TEST(BrowserShellAtlasTest, IgnoresTwoPixelIdentityJitter)
{
  BrowserShellAtlas atlas;
  atlas.reset(makeContext());
  const BrowserShellEntry original = makeEntry(
      {100, 100, 140, 140}, BrowserShellRole::Button,
      BrowserShellSource::UiaFast, 31, 4, 20);
  ASSERT_TRUE(atlas.merge(collectionWith(original), 20));
  const BrowserShellEntry jittered = makeEntry(
      {102, 98, 142, 138}, BrowserShellRole::Button,
      BrowserShellSource::UiaFast, 31, 4, 30);

  EXPECT_FALSE(atlas.merge(collectionWith(jittered), 30));
  const std::shared_ptr<const BrowserShellAtlasSnapshot> snapshot =
      atlas.makeSnapshot();
  ASSERT_EQ(snapshot->m_entry_count, 1U);
  EXPECT_EQ(snapshot->m_entries.at(0).m_hit_rect.left, 100);
  EXPECT_EQ(snapshot->m_entries.at(0).m_hit_rect.top, 100);
}

TEST(BrowserShellAtlasTest, RetainsBestSixtyFourEntriesWithoutOverflow)
{
  BrowserShellAtlas atlas;
  atlas.reset(makeContext());
  BrowserShellEntryCollection entries;
  for (std::size_t index = 0; index < BrowserShellAtlas::MaximumEntries;
       ++index)
  {
    ASSERT_TRUE(entries.append(makeEntry(
        {static_cast<int>(index * 20), 0, static_cast<int>(index * 20 + 10),
         10},
        BrowserShellRole::Unknown, BrowserShellSource::Geometry,
        1000 + index, static_cast<std::uint32_t>(index), 1, 1)));
  }
  ASSERT_TRUE(atlas.merge(entries, 1));

  const BrowserShellEntry preferred = makeEntry(
      {2000, 0, 2010, 10}, BrowserShellRole::Button,
      BrowserShellSource::UiaSemantic, 9999, 99, 100, 100);
  ASSERT_TRUE(atlas.merge(collectionWith(preferred), 100));
  const BrowserShellEntry rejected = makeEntry(
      {2020, 0, 2030, 10}, BrowserShellRole::Unknown,
      BrowserShellSource::Geometry, 9998, 98, 0, 0);
  EXPECT_FALSE(atlas.merge(collectionWith(rejected), 101));

  const std::shared_ptr<const BrowserShellAtlasSnapshot> snapshot =
      atlas.makeSnapshot();
  EXPECT_EQ(snapshot->m_entry_count, BrowserShellAtlas::MaximumEntries);
  EXPECT_TRUE(containsIdentity(*snapshot, 9999));
  EXPECT_FALSE(containsIdentity(*snapshot, 9998));
}

TEST(BrowserShellAtlasTest, ContextChangesAndInvalidationPreventReuse)
{
  const BrowserShellContext context = makeContext();
  BrowserShellAtlas atlas;
  atlas.reset(context);
  EXPECT_TRUE(atlas.matches(context));

  BrowserShellContext changed = context;
  changed.m_root_window =
      reinterpret_cast<HWND>(static_cast<std::uintptr_t>(0x2000));
  EXPECT_FALSE(atlas.matches(changed));
  changed = context;
  ++changed.m_process_id;
  EXPECT_FALSE(atlas.matches(changed));
  changed = context;
  ++changed.m_dpi;
  EXPECT_FALSE(atlas.matches(changed));
  changed = context;
  changed.m_full_screen = true;
  EXPECT_FALSE(atlas.matches(changed));
  changed = context;
  ++changed.m_window_rect.right;
  EXPECT_FALSE(atlas.matches(changed));

  atlas.invalidate();
  EXPECT_FALSE(atlas.matches(context));
}

TEST(BrowserShellAtlasTest, InvalidatesLayoutAndEntriesAfterConfirmedFailures)
{
  BrowserShellAtlas atlas;
  const BrowserShellContext context = makeContext();
  atlas.reset(context);
  const BrowserShellEntry entry = makeEntry(
      {0, 0, 40, 40}, BrowserShellRole::Button,
      BrowserShellSource::UiaFast, 51, 2, 10);
  ASSERT_TRUE(atlas.merge(collectionWith(entry), 10));

  atlas.advanceLayoutGeneration();
  BrowserShellEntry hit;
  EXPECT_FALSE(atlas.hitTest({20, 20}, hit));

  atlas.reset(context);
  ASSERT_TRUE(atlas.merge(collectionWith(entry), 10));
  atlas.recordCellMiss({20, 20});
  atlas.recordCellMiss({20, 20});
  EXPECT_TRUE(atlas.hitTest({20, 20}, hit));
  atlas.recordCellMiss({20, 20});
  EXPECT_FALSE(atlas.hitTest({20, 20}, hit));

  atlas.reset(context);
  ASSERT_TRUE(atlas.merge(collectionWith(entry), 10));
  atlas.recordEntryValidation(entry, false);
  atlas.recordEntryValidation(entry, true);
  atlas.recordEntryValidation(entry, false);
  EXPECT_TRUE(atlas.hitTest({20, 20}, hit));
  atlas.recordEntryValidation(entry, false);
  EXPECT_FALSE(atlas.hitTest({20, 20}, hit));
}

TEST(BrowserShellAtlasTest, DoesNotMergeHashCollisionsWithoutRelatedIdentity)
{
  BrowserShellAtlas atlas;
  atlas.reset(makeContext());
  BrowserShellEntryCollection entries;
  ASSERT_TRUE(entries.append(makeEntry(
      {0, 0, 30, 30}, BrowserShellRole::Button,
      BrowserShellSource::UiaFast, 61, 1)));
  ASSERT_TRUE(entries.append(makeEntry(
      {100, 0, 130, 30}, BrowserShellRole::BookmarkFolder,
      BrowserShellSource::UiaSemantic, 61, 2)));
  ASSERT_TRUE(atlas.merge(entries, 20));

  const std::shared_ptr<const BrowserShellAtlasSnapshot> snapshot =
      atlas.makeSnapshot();
  EXPECT_EQ(snapshot->m_entry_count, 2U);
}

TEST(BrowserShellAtlasTest, UsesHalfOpenRectsAcrossNegativeAndMultipleDisplays)
{
  BrowserShellAtlas atlas;
  atlas.reset(makeContext());
  BrowserShellEntryCollection entries;
  ASSERT_TRUE(entries.append(makeEntry(
      {-100, -20, 0, 20}, BrowserShellRole::Button,
      BrowserShellSource::UiaFast, 71, 1)));
  ASSERT_TRUE(entries.append(makeEntry(
      {1920, 0, 2000, 40}, BrowserShellRole::Tab,
      BrowserShellSource::UiaFast, 72, 2)));
  ASSERT_TRUE(atlas.merge(entries, 20));

  BrowserShellEntry hit;
  EXPECT_TRUE(atlas.hitTest({-100, -20}, hit));
  EXPECT_FALSE(atlas.hitTest({0, 0}, hit));
  EXPECT_TRUE(atlas.hitTest({1999, 39}, hit));
  EXPECT_FALSE(atlas.hitTest({2000, 39}, hit));
}

TEST(BrowserShellAtlasTest, KeepsNegativeAndPositiveMissCellsSeparate)
{
  BrowserShellAtlas atlas;
  atlas.reset(makeContext());
  const BrowserShellEntry entry = makeEntry(
      {-20, -20, 20, 20}, BrowserShellRole::Button,
      BrowserShellSource::UiaFast, 81, 1);
  ASSERT_TRUE(atlas.merge(collectionWith(entry), 20));

  atlas.recordCellMiss({-1, 0});
  atlas.recordCellMiss({-1, 0});
  atlas.recordCellMiss({0, 0});

  BrowserShellEntry hit;
  EXPECT_TRUE(atlas.hitTest({0, 0}, hit));
}

TEST(BrowserShellAtlasTest,
     SnapshotHitRequiresCurrentContextAndKeepsExtensionBounds)
{
  BrowserShellAtlas atlas;
  const BrowserShellContext context = makeContext();
  atlas.reset(context);

  BrowserShellEntryCollection entries;
  ASSERT_TRUE(entries.append(makeEntry(
      {220, 20, 256, 52}, BrowserShellRole::ExtensionButton,
      BrowserShellSource::UiaSemantic, 98, 81, 31)));
  ASSERT_TRUE(atlas.merge(entries, 100));
  const std::shared_ptr<const BrowserShellAtlasSnapshot> snapshot =
      atlas.makeSnapshot();
  ASSERT_NE(snapshot, nullptr);

  BrowserShellEntry hit;
  EXPECT_TRUE(BrowserShellAtlas::hitTestSnapshot(
      *snapshot, context, {230, 30}, hit));
  EXPECT_EQ(hit.m_role, BrowserShellRole::ExtensionButton);
  EXPECT_EQ(hit.m_hit_rect.left, 220);
  EXPECT_EQ(hit.m_hit_rect.top, 20);
  EXPECT_EQ(hit.m_hit_rect.right, 256);
  EXPECT_EQ(hit.m_hit_rect.bottom, 52);

  BrowserShellContext changed_context = context;
  ++changed_context.m_window_generation;
  EXPECT_FALSE(BrowserShellAtlas::hitTestSnapshot(
      *snapshot, changed_context, {230, 30}, hit));
}

}  // namespace
}  // namespace qingying
