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
