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

// ---- #1989: SW1 on a main list screen ---------------------------------------------

using keynav::ListTap;
using keynav::listTap;

constexpr bool kNoKbd = false, kKbd = true;
constexpr bool kOnScreens = false, kInList = true;

// On the screens: the owner's rule, a tap forward and a double tap back, whatever the list
// holds. This is what #1884 got wrong -- it walked the rows first.
TEST(ListTap, OnTheScreensATapMovesToTheNextScreen) {
  EXPECT_EQ(ListTap::CycleNext, listTap(KEY_NEXT, kNoKbd, kOnScreens, 0, 40).action);
}

TEST(ListTap, OnTheScreensADoubleTapMovesToThePreviousScreen) {
  EXPECT_EQ(ListTap::CyclePrev, listTap(KEY_PREV, kNoKbd, kOnScreens, 5, 40).action);
}

TEST(ListTap, OnTheScreensATapNeverWalksTheList) {
  for (int sel = 0; sel < 3; sel++) {
    EXPECT_NE(ListTap::Select, listTap(KEY_NEXT, kNoKbd, kOnScreens, sel, 3).action) << sel;
    EXPECT_NE(ListTap::Select, listTap(KEY_PREV, kNoKbd, kOnScreens, sel, 3).action) << sel;
  }
}

TEST(ListTap, OnTheScreensALongPressEntersTheListAtItsTop) {
  const auto r = listTap(KEY_ENTER, kNoKbd, kOnScreens, 7, 10);
  EXPECT_EQ(ListTap::EnterList, r.action);
  EXPECT_EQ(0, r.sel) << "the list is entered at the top, not at a remembered row";
}

TEST(ListTap, OnTheScreensALongPressOpensNothing) {
  EXPECT_NE(ListTap::Open, listTap(KEY_ENTER, kNoKbd, kOnScreens, 0, 3).action);
}

TEST(ListTap, ALongPressOnAnEmptyListDoesNothing) {
  EXPECT_EQ(ListTap::None, listTap(KEY_ENTER, kNoKbd, kOnScreens, 0, 0).action);
}

// In the list.
TEST(ListTap, InTheListATapMovesDown) {
  const auto r = listTap(KEY_NEXT, kNoKbd, kInList, 0, 3);
  EXPECT_EQ(ListTap::Select, r.action);
  EXPECT_EQ(1, r.sel);
}

TEST(ListTap, InTheListADoubleTapMovesUp) {
  const auto r = listTap(KEY_PREV, kNoKbd, kInList, 2, 3);
  EXPECT_EQ(ListTap::Select, r.action);
  EXPECT_EQ(1, r.sel);
}

TEST(ListTap, ADoubleTapOnTheTopRowReturnsToTheScreens) {
  EXPECT_EQ(ListTap::LeaveList, listTap(KEY_PREV, kNoKbd, kInList, 0, 3).action);
}

TEST(ListTap, ATapOnTheLastRowStaysThere) {
  const auto r = listTap(KEY_NEXT, kNoKbd, kInList, 2, 3);
  EXPECT_EQ(ListTap::Select, r.action);
  EXPECT_EQ(2, r.sel);
}

TEST(ListTap, ATapInTheListNeverChangesScreen) {
  for (int sel = 0; sel < 3; sel++) {
    for (uint8_t key : {(uint8_t)KEY_NEXT, (uint8_t)KEY_PREV}) {
      const ListTap a = listTap(key, kNoKbd, kInList, sel, 3).action;
      EXPECT_NE(ListTap::CycleNext, a) << sel;
      EXPECT_NE(ListTap::CyclePrev, a) << sel;
    }
  }
}

TEST(ListTap, InTheListALongPressOpensTheRow) {
  const auto r = listTap(KEY_ENTER, kNoKbd, kInList, 1, 3);
  EXPECT_EQ(ListTap::Open, r.action);
  EXPECT_EQ(1, r.sel);
}

TEST(ListTap, ALongPressOnAStaleSelectionOpensARowThatExists) {
  const auto r = listTap(KEY_ENTER, kNoKbd, kInList, 5, 3);
  EXPECT_EQ(ListTap::Open, r.action);
  EXPECT_EQ(2, r.sel);
}

TEST(ListTap, AListThatEmptiedHandsTheFocusBack) {
  EXPECT_EQ(ListTap::LeaveList, listTap(KEY_NEXT, kNoKbd, kInList, 0, 0).action);
  EXPECT_EQ(ListTap::LeaveList, listTap(KEY_PREV, kNoKbd, kInList, 0, 0).action);
  EXPECT_EQ(ListTap::LeaveList, listTap(KEY_ENTER, kNoKbd, kInList, 0, 0).action);
}

TEST(ListTap, AStaleSelectionPastTheEndStepsBackOntoTheList) {
  auto r = listTap(KEY_PREV, kNoKbd, kInList, 5, 3);
  EXPECT_EQ(ListTap::Select, r.action);
  EXPECT_EQ(2, r.sel);
  r = listTap(KEY_NEXT, kNoKbd, kInList, 5, 3);
  EXPECT_EQ(ListTap::Select, r.action);
  EXPECT_EQ(2, r.sel);
}

// A keyboard badge keeps its design: a tap cycles, the arrow keys walk the list, and a
// long press opens the highlighted row, wherever the focus flag happens to be.
TEST(ListTap, WithAKeyboardFittedATapCyclesScreens) {
  for (bool in_list : {kOnScreens, kInList}) {
    EXPECT_EQ(ListTap::CycleNext, listTap(KEY_NEXT, kKbd, in_list, 0, 3).action);
    EXPECT_EQ(ListTap::CyclePrev, listTap(KEY_PREV, kKbd, in_list, 2, 3).action);
  }
}

TEST(ListTap, WithAKeyboardFittedALongPressOpensTheRow) {
  const auto r = listTap(KEY_ENTER, kKbd, kOnScreens, 2, 3);
  EXPECT_EQ(ListTap::Open, r.action);
  EXPECT_EQ(2, r.sel);
  EXPECT_EQ(ListTap::None, listTap(KEY_ENTER, kKbd, kOnScreens, 0, 0).action);
}

TEST(ListTap, OtherKeysAreNotATap) {
  for (bool in_list : {kOnScreens, kInList}) {
    EXPECT_EQ(ListTap::None, listTap(KEY_UP, kNoKbd, in_list, 1, 3).action);
    EXPECT_EQ(ListTap::None, listTap(KEY_CANCEL, kNoKbd, in_list, 1, 3).action);
    EXPECT_EQ(ListTap::None, listTap((uint8_t)'a', kKbd, in_list, 1, 3).action);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
