#include <gtest/gtest.h>
#include <helpers/ui/KeyNav.h>

using keynav::Step;

TEST(KeyNav, LeftUpAndPrevGoBack) {
  EXPECT_EQ(Step::Prev, keynav::pageStep(KEY_LEFT));
  EXPECT_EQ(Step::Prev, keynav::pageStep(KEY_UP));
  EXPECT_EQ(Step::Prev, keynav::pageStep(KEY_PREV));   // SW1 double-click
}

TEST(KeyNav, RightDownAndNextGoOn) {
  EXPECT_EQ(Step::Next, keynav::pageStep(KEY_RIGHT));
  EXPECT_EQ(Step::Next, keynav::pageStep(KEY_DOWN));
  EXPECT_EQ(Step::Next, keynav::pageStep(KEY_NEXT));   // SW1 click
}

TEST(KeyNav, OtherKeysDoNotMovePages) {
  for (uint8_t key : {(uint8_t)KEY_ENTER, (uint8_t)KEY_CANCEL, (uint8_t)KEY_SELECT,
                      (uint8_t)KEY_BACKSPACE, (uint8_t)KEY_TAB, (uint8_t)'a', (uint8_t)' ',
                      (uint8_t)0}) {
    EXPECT_EQ(Step::None, keynav::pageStep(key)) << int(key);
  }
}

TEST(KeyNav, KeysAbove127SurviveTheCharPlumbing) {
  // Screens take input as `char`, and on some targets char is signed. The arrow and
  // SW1 codes are all above 127, so they must still be recognized after the round trip.
  const char left = (char)KEY_LEFT, down = (char)KEY_DOWN, next = (char)KEY_NEXT;
  EXPECT_EQ(Step::Prev, keynav::pageStep((uint8_t)left));
  EXPECT_EQ(Step::Next, keynav::pageStep((uint8_t)down));
  EXPECT_EQ(Step::Next, keynav::pageStep((uint8_t)next));
}

TEST(KeyNav, PagesWrapAtBothEnds) {
  EXPECT_EQ(4, keynav::stepPage(0, 5, Step::Prev));
  EXPECT_EQ(0, keynav::stepPage(4, 5, Step::Next));
  EXPECT_EQ(2, keynav::stepPage(1, 5, Step::Next));
  EXPECT_EQ(1, keynav::stepPage(2, 5, Step::Prev));
  EXPECT_EQ(3, keynav::stepPage(3, 5, Step::None));
  EXPECT_EQ(0, keynav::stepPage(0, 0, Step::Next));   // no pages: stay put
}

TEST(KeyNav, OnlyEscBacksOut) {
  EXPECT_TRUE(keynav::backsOut(KEY_CANCEL));
  EXPECT_FALSE(keynav::backsOut(KEY_ENTER));
  EXPECT_FALSE(keynav::backsOut(KEY_LEFT));
  EXPECT_FALSE(keynav::backsOut((uint8_t)'q'));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
