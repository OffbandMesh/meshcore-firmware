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

// #1590: a badge with no keyboard has no Esc key, and backsOut() accepts only KEY_CANCEL.
// So before this, a button-only board could not leave Settings AT ALL -- which did not
// matter while such a board had no display either, and stops being true the moment someone
// solders one on.
//
// All four SW1 gestures were already taken, so the exit shares the triple-click: it backs
// out where there is somewhere to back out to, and keeps toggling the buzzer at Home, where
// there is not. The two can never compete, because "get me out of here" is meaningless on
// the Home screen.
TEST(KeyNav, TripleClickBacksOutAwayFromHome) {
  EXPECT_EQ((uint8_t)KEY_CANCEL, keynav::tripleClickKey(false));
  // And the key it produces must be one backsOut() actually accepts, or the gesture is
  // inert and the board is still stuck.
  EXPECT_TRUE(keynav::backsOut(keynav::tripleClickKey(false)));
}

TEST(KeyNav, TripleClickKeepsItsOtherJobAtHome) {
  EXPECT_EQ((uint8_t)0, keynav::tripleClickKey(true))
      << "a triple-click at Home must fall through to the buzzer toggle, not back out";
  EXPECT_FALSE(keynav::backsOut(keynav::tripleClickKey(true)));
}

// ---- #1884: a tap on a main list screen ---------------------------------------------

using keynav::ListTap;
using keynav::listTap;

TEST(ListTap, WithoutAKeyboardATapWalksDownTheList) {
  const auto r = listTap(KEY_NEXT, false, 0, 3);
  EXPECT_EQ(ListTap::Select, r.action);
  EXPECT_EQ(1, r.sel);
}

TEST(ListTap, WithoutAKeyboardADoubleTapWalksUpTheList) {
  const auto r = listTap(KEY_PREV, false, 2, 3);
  EXPECT_EQ(ListTap::Select, r.action);
  EXPECT_EQ(1, r.sel);
}

TEST(ListTap, WalkingOffTheBottomMovesToTheNextScreen) {
  EXPECT_EQ(ListTap::CycleNext, listTap(KEY_NEXT, false, 2, 3).action);
}

TEST(ListTap, WalkingOffTheTopMovesToThePreviousScreen) {
  EXPECT_EQ(ListTap::CyclePrev, listTap(KEY_PREV, false, 0, 3).action);
}

TEST(ListTap, AnEmptyListMovesStraightOn) {
  EXPECT_EQ(ListTap::CycleNext, listTap(KEY_NEXT, false, 0, 0).action);
  EXPECT_EQ(ListTap::CyclePrev, listTap(KEY_PREV, false, 0, 0).action);
}

TEST(ListTap, AOneRowListMovesStraightOnBothWays) {
  EXPECT_EQ(ListTap::CycleNext, listTap(KEY_NEXT, false, 0, 1).action);
  EXPECT_EQ(ListTap::CyclePrev, listTap(KEY_PREV, false, 0, 1).action);
}

TEST(ListTap, WithAKeyboardFittedATapStillCyclesScreens) {
  // The supporter badge keeps its design: the arrow keys walk the list, a tap cycles.
  EXPECT_EQ(ListTap::CycleNext, listTap(KEY_NEXT, true, 0, 3).action);
  EXPECT_EQ(ListTap::CyclePrev, listTap(KEY_PREV, true, 2, 3).action);
}

TEST(ListTap, AStaleSelectionPastTheEndStepsBackOntoTheList) {
  const auto r = listTap(KEY_PREV, false, 5, 3);
  EXPECT_EQ(ListTap::Select, r.action);
  EXPECT_EQ(2, r.sel);
  EXPECT_EQ(ListTap::CycleNext, listTap(KEY_NEXT, false, 5, 3).action);
}

using keynav::ListEntry;
using keynav::listEntry;

TEST(ListEntry, WithoutAKeyboardAForwardStepStartsAtTheTop) {
  EXPECT_EQ(ListEntry::Top, listEntry(1, false));
}

TEST(ListEntry, WithoutAKeyboardABackwardStepStartsAtTheBottom) {
  // Otherwise a double tap off the top of Nearby lands on Contacts' top row and the next
  // double tap leaves at once: a list could never be walked up from its end.
  EXPECT_EQ(ListEntry::Bottom, listEntry(-1, false));
}

TEST(ListEntry, WithAKeyboardTheRememberedRowIsKept) {
  EXPECT_EQ(ListEntry::Keep, listEntry(1, true));
  EXPECT_EQ(ListEntry::Keep, listEntry(-1, true));
}

TEST(ListEntry, NoStepKeepsTheRow) {
  EXPECT_EQ(ListEntry::Keep, listEntry(0, false));
}

TEST(ListTap, OtherKeysAreNotATap) {
  EXPECT_EQ(ListTap::None, listTap(KEY_ENTER, false, 0, 3).action);
  EXPECT_EQ(ListTap::None, listTap(KEY_UP, false, 1, 3).action);
  EXPECT_EQ(ListTap::None, listTap(KEY_CANCEL, true, 1, 3).action);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
