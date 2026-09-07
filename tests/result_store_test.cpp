#include "qingying/app/result_store.h"

#include <gtest/gtest.h>

#include <utility>

namespace qingying {
namespace {

Image makeImage(std::uint32_t pixel) {
  Image image;
  image.width = 2;
  image.height = 1;
  image.pixels = {pixel, pixel};
  return image;
}

}  // namespace

TEST(ResultStoreTest, EmptyByDefault) {
  ResultStore store;

  EXPECT_EQ(store.currentId(), kInvalidResultId);
  EXPECT_FALSE(store.current().has_value());
  EXPECT_FALSE(store.get(1).has_value());
}

TEST(ResultStoreTest, PublishReplacesPreviousResult) {
  ResultStore store;

  const ResultId first_id = store.publish(makeImage(0xFF112233u));
  const ResultId second_id = store.publish(makeImage(0xFF445566u));

  ASSERT_NE(first_id, kInvalidResultId);
  ASSERT_NE(second_id, kInvalidResultId);
  EXPECT_NE(first_id, second_id);
  EXPECT_EQ(store.currentId(), second_id);

  EXPECT_FALSE(store.get(first_id).has_value());

  const auto current = store.current();
  ASSERT_TRUE(current.has_value());
  EXPECT_EQ(current->result_id, second_id);
  EXPECT_EQ(current->image.pixels[0], 0xFF445566u);
}

TEST(ResultStoreTest, ResolvesCurrentAndExplicitResultSelections) {
  ResultStore store;
  const ResultId first_id = store.publish(makeImage(0xFF112233u));
  const ResultId second_id = store.publish(makeImage(0xFF445566u));

  EXPECT_EQ(store.resolve(ResultSelection::current()), second_id);
  EXPECT_EQ(store.resolve(ResultSelection::specific(first_id)), first_id);
  EXPECT_FALSE(store.get(first_id).has_value());
  EXPECT_EQ(store.resolve(ResultSelection{
                ResultSelectionKind::Explicit, kInvalidResultId}),
            kInvalidResultId);
}

TEST(ResultStoreTest, InvalidPublishDoesNotReplaceCurrentResult) {
  ResultStore store;
  const ResultId current_id = store.publish(makeImage(0xFF112233u));

  Image invalid;
  invalid.width = 2;
  invalid.height = 1;
  invalid.pixels = {0xFFFFFFFFu};

  EXPECT_EQ(store.publish(std::move(invalid)), kInvalidResultId);
  EXPECT_EQ(store.currentId(), current_id);
  ASSERT_TRUE(store.current().has_value());
  EXPECT_EQ(store.current()->image.pixels[0], 0xFF112233u);
}

TEST(ResultStoreTest, ClearDropsAllPublishedResults) {
  ResultStore store;
  const ResultId result_id = store.publish(makeImage(0xFF112233u));

  store.clear();

  EXPECT_EQ(store.currentId(), kInvalidResultId);
  EXPECT_FALSE(store.current().has_value());
  EXPECT_FALSE(store.get(result_id).has_value());
}

TEST(ResultStoreTest, ReleaseDropsTheSelectedResult) {
  ResultStore store;
  const ResultId result_id = store.publish(makeImage(0xFF112233u));

  store.release(result_id);

  EXPECT_EQ(store.currentId(), kInvalidResultId);
  EXPECT_FALSE(store.current().has_value());
  EXPECT_FALSE(store.get(result_id).has_value());
}

}  // namespace qingying

namespace qingying {
TEST(ResultStoreTest, ScopesRejectForeignIdsAndClearIndependently) {
  ResultStore store;
  const auto gui = store.publish(Image{1, 1, {11}});
  const auto a = store.publish(2, Image{1, 1, {22}});
  const auto b = store.publish(3, Image{1, 1, {33}});
  EXPECT_FALSE(store.acquire(2, b));
  EXPECT_FALSE(store.acquire(3, gui));
  EXPECT_FALSE(store.acquire(kGuiResultScopeId, a));
  EXPECT_FALSE(store.get(a));
  store.release(2, b);
  EXPECT_TRUE(store.acquire(3, b));
  store.clear();
  EXPECT_FALSE(store.acquire(kGuiResultScopeId, gui));
  EXPECT_EQ(store.acquire(2, ResultSelection::current()).image()->pixels[0], 22u);
  store.clearScope(2);
  EXPECT_FALSE(store.acquire(2, a));
  EXPECT_TRUE(store.acquire(3, b));
  store.clearAll();
  EXPECT_FALSE(store.acquire(3, b));
}
TEST(ResultStoreTest, LeaseSurvivesReplacementReleaseAndStoreDestruction) {
  ResultLease retained;
  {
    ResultStore store;
    const auto id = store.publish(2, Image{2, 1, {11, 22}}, {-5, 7, 2, 1});
    retained = store.acquire(2, id);
    ASSERT_TRUE(retained);
    EXPECT_EQ(retained.image(), store.acquire(2, id).image());
    const auto next = store.publish(2, Image{1, 1, {33}});
    EXPECT_FALSE(store.acquire(2, id));
    store.release(2, next);
    EXPECT_EQ(retained.image()->pixels[1], 22u);
  }
  ASSERT_TRUE(retained);
  EXPECT_EQ(retained.image()->pixels[0], 11u);
  EXPECT_EQ(retained.metadata().bounds, (ScreenPhysicalRect{-5, 7, 2, 1}));
  EXPECT_EQ(retained.metadata().width, 2);
}
TEST(ResultStoreTest, InvalidScopeOrSelectionCannotAccessOrPublish) {
  ResultStore store;
  EXPECT_EQ(store.publish(0, Image{1, 1, {1}}), kInvalidResultId);
  const auto id = store.publish(2, Image{1, 1, {2}});
  EXPECT_FALSE(store.acquire(0, id));
  EXPECT_FALSE(store.acquire(2, ResultSelection{ResultSelectionKind::Current, id}));
  EXPECT_EQ(store.publish(2, Image{2, 1, {1}}), kInvalidResultId);
  EXPECT_TRUE(store.acquire(2, id));
}
}  // namespace qingying
