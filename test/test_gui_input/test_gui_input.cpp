// #1443: button timing and the screen's on/dim/off states. These decide
// whether a hold means Back or Sleep, and whether the screen stays lit while
// someone is reading it.

#include <gtest/gtest.h>

#include "helpers/gui/InputTiming.h"

using namespace offband::gui;

namespace {

// Holds the button down from `t0` for `held` ms, polling every 5 ms, and
// returns the kind reported on release (or fails if none or several).
PressKind press(ButtonTracker& b, uint32_t t0, uint32_t held) {
  PressKind kind = PressKind::Press;
  int reports = 0;
  for (uint32_t t = t0; t < t0 + held; t += 5) {
    if (b.update(true, t, &kind)) reports++;
  }
  for (uint32_t t = t0 + held; t < t0 + held + 60; t += 5) {
    if (b.update(false, t, &kind)) reports++;
  }
  EXPECT_EQ(reports, 1);
  return kind;
}

}  // namespace

TEST(ButtonTracker, ClassifiesByHowLongItWasHeld) {
  ButtonTracker b;
  EXPECT_EQ(press(b, 1000, 100), PressKind::Press);
  EXPECT_EQ(press(b, 2000, 700), PressKind::Hold);
  EXPECT_EQ(press(b, 4000, 3100), PressKind::LongHold);
}

TEST(ButtonTracker, ReportsOnlyOnRelease) {
  ButtonTracker b;
  PressKind kind;
  for (uint32_t t = 0; t < 4000; t += 5) EXPECT_FALSE(b.update(true, t, &kind));
}

TEST(ButtonTracker, ContactBounceShorterThanDebounceIsIgnored) {
  ButtonTracker b;
  PressKind kind;
  int reports = 0;
  // 10 ms blips: never stable for 20 ms, so never a press.
  for (uint32_t t = 0; t < 200; t += 10) {
    if (b.update((t / 10) % 2 == 0, t, &kind)) reports++;
  }
  for (uint32_t t = 200; t < 300; t += 5) {
    if (b.update(false, t, &kind)) reports++;
  }
  EXPECT_EQ(reports, 0);
}

TEST(ScreenTimer, OnThenDimThenOff) {
  EXPECT_EQ(screenStateFor(0, 30000, 120000, false), ScreenState::On);
  EXPECT_EQ(screenStateFor(29999, 30000, 120000, false), ScreenState::On);
  EXPECT_EQ(screenStateFor(30000, 30000, 120000, false), ScreenState::Dim);
  EXPECT_EQ(screenStateFor(120000, 30000, 120000, false), ScreenState::Off);
}

TEST(ScreenTimer, ZeroDisablesThatStep) {
  EXPECT_EQ(screenStateFor(500000, 0, 120000, false), ScreenState::Off);
  EXPECT_EQ(screenStateFor(500000, 30000, 0, false), ScreenState::Dim);
  EXPECT_EQ(screenStateFor(500000, 0, 0, false), ScreenState::On);
}

TEST(ScreenTimer, AlwaysOnWins) {
  EXPECT_EQ(screenStateFor(10000000, 30000, 120000, true), ScreenState::On);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
