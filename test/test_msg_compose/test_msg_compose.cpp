// Native unit tests for composing a message on the badge's keyboard (#1228): how many
// characters a message may carry, and a line that stops at that budget.

#include <gtest/gtest.h>
#include <string>
#include "helpers/ui/MsgCompose.h"

// MeshCore caps a text payload at MAX_TEXT_LEN (160) bytes. A DM gets all of it; a
// channel message spends name + 2 of them on "<name>: ".
TEST(ComposeBudget, DirectMessageGetsTheWholePayload) {
  EXPECT_EQ(160, compose::budget(false, 12, 160));
}

TEST(ComposeBudget, ChannelMessageLosesTheNamePrefix) {
  EXPECT_EQ(160 - 12 - 2, compose::budget(true, 12, 160));
}

TEST(ComposeBudget, NameTooLongLeavesNothing) {
  EXPECT_EQ(0, compose::budget(true, 158, 160));
  EXPECT_EQ(0, compose::budget(true, 200, 160));
}

TEST(Composer, StopsAtTheBudget) {
  compose::Composer<160> c;
  c.start(3);
  EXPECT_TRUE(c.apply('a'));
  EXPECT_TRUE(c.apply('b'));
  EXPECT_TRUE(c.apply('c'));
  EXPECT_FALSE(c.apply('d'));
  EXPECT_STREQ("abc", c.text());
  EXPECT_EQ(0, c.remaining());
}

TEST(Composer, BackspaceStillWorksAtTheBudget) {
  compose::Composer<160> c;
  c.start(2);
  c.apply('a');
  c.apply('b');
  EXPECT_TRUE(c.apply(KEY_BACKSPACE));
  EXPECT_STREQ("a", c.text());
  EXPECT_EQ(1, c.remaining());
}

TEST(Composer, TabCountsAsASpace) {
  compose::Composer<160> c;
  c.start(1);
  EXPECT_TRUE(c.apply(KEY_TAB));
  EXPECT_STREQ(" ", c.text());
  EXPECT_FALSE(c.apply('x'));
}

TEST(Composer, NavigationKeysDoNotType) {
  compose::Composer<160> c;
  c.start(10);
  EXPECT_FALSE(c.apply(KEY_LEFT));
  EXPECT_FALSE(c.apply(KEY_ENTER));
  EXPECT_FALSE(c.apply(KEY_CANCEL));
  EXPECT_EQ(0u, c.length());
}

TEST(Composer, BudgetNeverExceedsTheBuffer) {
  compose::Composer<4> c;
  c.start(160);
  EXPECT_EQ(4, c.remaining());
  for (char ch : std::string("abcdef")) c.apply((uint8_t)ch);
  EXPECT_STREQ("abcd", c.text());
}

TEST(Composer, StartClearsTheLine) {
  compose::Composer<160> c;
  c.start(10);
  c.apply('a');
  c.start(10);
  EXPECT_EQ(0u, c.length());
  EXPECT_EQ(10, c.remaining());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
