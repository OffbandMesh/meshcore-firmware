#include <gtest/gtest.h>
#include <helpers/ui/LineEdit.h>

TEST(LineEdit, StartsEmpty) {
  LineEdit<8> line;
  EXPECT_STREQ("", line.text());
  EXPECT_EQ(0u, line.length());
}

TEST(LineEdit, PrintablesAppend) {
  LineEdit<8> line;
  EXPECT_TRUE(line.apply('h'));
  EXPECT_TRUE(line.apply('i'));
  EXPECT_TRUE(line.apply('!'));
  EXPECT_STREQ("hi!", line.text());
}

TEST(LineEdit, BackspaceDeletesTheLastCharacter) {
  LineEdit<8> line;
  line.apply('a');
  line.apply('b');
  EXPECT_TRUE(line.apply(KEY_BACKSPACE));
  EXPECT_STREQ("a", line.text());
  EXPECT_TRUE(line.apply(KEY_BACKSPACE));
  EXPECT_FALSE(line.apply(KEY_BACKSPACE));   // nothing left to delete
  EXPECT_STREQ("", line.text());
}

TEST(LineEdit, TabInsertsASpace) {
  LineEdit<8> line;
  line.apply('a');
  EXPECT_TRUE(line.apply(KEY_TAB));
  line.apply('b');
  EXPECT_STREQ("a b", line.text());
}

TEST(LineEdit, AFullLineRefusesMoreAndStaysTerminated) {
  LineEdit<3> line;
  EXPECT_TRUE(line.apply('x'));
  EXPECT_TRUE(line.apply('y'));
  EXPECT_TRUE(line.apply('z'));
  EXPECT_FALSE(line.apply('w'));
  EXPECT_STREQ("xyz", line.text());
  EXPECT_EQ(3u, line.length());
}

TEST(LineEdit, NavigationAndControlKeysAreLeftToTheCaller) {
  LineEdit<8> line;
  for (uint8_t key : {(uint8_t)KEY_LEFT, (uint8_t)KEY_UP, (uint8_t)KEY_DOWN, (uint8_t)KEY_RIGHT,
                      (uint8_t)KEY_ENTER, (uint8_t)KEY_CANCEL, (uint8_t)KEY_NEXT, (uint8_t)0}) {
    EXPECT_FALSE(line.apply(key)) << int(key);
  }
  EXPECT_STREQ("", line.text());
}

TEST(LineEdit, ClearEmptiesTheLine) {
  LineEdit<8> line;
  line.apply('a');
  line.clear();
  EXPECT_STREQ("", line.text());
  EXPECT_EQ(0u, line.length());
}

TEST(LineEdit, TailShowsTheEndOfALongLine) {
  LineEdit<16> line;
  for (char ch : {'a', 'b', 'c', 'd', 'e', 'f'}) line.apply(ch);
  EXPECT_STREQ("def", line.tail(3));
  EXPECT_STREQ("abcdef", line.tail(10));   // shorter than the width: the whole line
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
