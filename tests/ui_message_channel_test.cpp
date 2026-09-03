#include "qingying/app/ui_message_channel.h"

#include <gtest/gtest.h>

#include <string>

namespace qingying {
namespace {

struct TestMessage {
  int value{0};
  std::string text;
};

TEST(UiMessageChannelTest, TakesTypedPayloadAndRemovesIt) {
  UiMessageChannel channel;

  const auto token = channel.push(TestMessage{42, "owned"});
  ASSERT_TRUE(token.has_value());
  EXPECT_EQ(channel.size(), 1u);

  const auto message = channel.take<TestMessage>(*token);
  ASSERT_TRUE(message.has_value());
  EXPECT_EQ(message->value, 42);
  EXPECT_EQ(message->text, "owned");
  EXPECT_EQ(channel.size(), 0u);
}

TEST(UiMessageChannelTest, DiscardAndDrainReleaseUnconsumedTokens) {
  UiMessageChannel channel;

  const auto discarded = channel.push(TestMessage{1, "discarded"});
  const auto drained = channel.push(TestMessage{2, "drained"});
  ASSERT_TRUE(discarded.has_value());
  ASSERT_TRUE(drained.has_value());

  EXPECT_TRUE(channel.discard(*discarded));
  EXPECT_EQ(channel.size(), 1u);
  channel.drain();
  EXPECT_EQ(channel.size(), 0u);
  EXPECT_FALSE(channel.take<TestMessage>(*drained).has_value());
}

TEST(UiMessageChannelTest, TokensAreNotReusedAfterDrain) {
  UiMessageChannel channel;

  const auto first = channel.push(TestMessage{1, "first"});
  ASSERT_TRUE(first.has_value());
  channel.drain();

  const auto second = channel.push(TestMessage{2, "second"});
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(*first, *second);
}

}  // namespace
}  // namespace qingying
