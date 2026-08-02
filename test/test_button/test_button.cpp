// #527: press-counting state machine tests.
//
// The defect this replaces: a triple-press resolved as a SINGLE on real hardware,
// because presses that were not sampled at the right instant were discarded with no
// counter increment, no event and no log. These tests exist so that failure mode is
// impossible to reintroduce silently -- they feed edges with explicit timestamps, the
// way a real burst arrives, rather than depending on when a loop happened to look.
#include <gtest/gtest.h>
#include "../../examples/companion_radio/ui-orig/ButtonSequencer.h"

using E = ButtonSequencer::Event;

// Helper: press down at `t`, release at `t+hold`.
static void click(ButtonSequencer& b, uint32_t t, uint32_t hold = 60) {
  b.onEdge(t, true);
  b.onEdge(t + hold, false);
}

// Drain tick() at `now` and return the single resolved event (EV_NONE if nothing).
static E settle(ButtonSequencer& b, uint32_t now) { return b.tick(now); }

// ------------------------------------------------------------------ basic counts
TEST(Seq, SingleClick) {
  ButtonSequencer b; b.reset();
  click(b, 1000);
  EXPECT_EQ(settle(b, 1000 + 60 + BUTTON_CLICK_TIMEOUT_MS), E::EV_SHORT);
}
TEST(Seq, DoubleClick) {
  ButtonSequencer b; b.reset();
  click(b, 1000); click(b, 1200);
  EXPECT_EQ(settle(b, 1200 + 60 + BUTTON_CLICK_TIMEOUT_MS), E::EV_DOUBLE);
}
TEST(Seq, TripleClick) {
  ButtonSequencer b; b.reset();
  click(b, 1000); click(b, 1200); click(b, 1400);
  EXPECT_EQ(settle(b, 1400 + 60 + BUTTON_CLICK_TIMEOUT_MS), E::EV_TRIPLE);
}
TEST(Seq, QuadrupleClick) {
  ButtonSequencer b; b.reset();
  click(b, 1000); click(b, 1150); click(b, 1300); click(b, 1450);
  EXPECT_EQ(settle(b, 1450 + 60 + BUTTON_CLICK_TIMEOUT_MS), E::EV_QUADRUPLE);
}
TEST(Seq, FiveClicksClampToQuadruple) {
  ButtonSequencer b; b.reset();
  for (int i = 0; i < 5; i++) click(b, 1000 + i * 150);
  EXPECT_EQ(settle(b, 2000 + BUTTON_CLICK_TIMEOUT_MS), E::EV_QUADRUPLE);
}

// -------------------------------------------------- THE FIELD FAILURE: a late loop
// Four taps in under a second, then the consumer does not tick() for 800 ms because
// the main loop was busy. The old implementation lost three of these outright. The
// count must survive, because the edges carry their own timestamps.
TEST(Seq, LateDrainStillResolvesQuadruple) {
  ButtonSequencer b; b.reset();
  click(b, 1000, 80); click(b, 1160, 80); click(b, 1320, 80); click(b, 1480, 80);
  // nothing ticks for 800ms after the last release
  EXPECT_EQ(settle(b, 1560 + 800), E::EV_QUADRUPLE);
}
TEST(Seq, EdgesDeliveredInOneBatchAfterStall) {
  ButtonSequencer b; b.reset();
  // all edges captured during a stall, fed at once, timestamps preserved
  b.onEdge(2000, true);  b.onEdge(2070, false);
  b.onEdge(2200, true);  b.onEdge(2270, false);
  b.onEdge(2400, true);  b.onEdge(2470, false);
  EXPECT_EQ(settle(b, 3500), E::EV_TRIPLE);
}

// ------------------------------------------------------------------- press edges
TEST(Seq, PressEdgeReportedPerPress) {
  ButtonSequencer b; b.reset();
  EXPECT_EQ(b.onEdge(1000, true),  E::EV_PRESS_EDGE);
  EXPECT_EQ(b.onEdge(1060, false), E::EV_NONE);
  EXPECT_EQ(b.onEdge(1200, true),  E::EV_PRESS_EDGE);   // second press also reports
}

// ------------------------------------------------------------------ debounce
TEST(Seq, BounceBurstCountsOnce) {
  ButtonSequencer b; b.reset();
  b.onEdge(1000, true);
  b.onEdge(1003, false);   // chatter, inside debounce
  b.onEdge(1006, true);    // chatter
  b.onEdge(1009, false);   // chatter
  b.onEdge(1080, false);   // real release, past debounce
  EXPECT_EQ(settle(b, 1080 + BUTTON_CLICK_TIMEOUT_MS), E::EV_SHORT);
}
TEST(Seq, RepeatedSameLevelIsNotATransition) {
  ButtonSequencer b; b.reset();
  b.onEdge(1000, true);
  EXPECT_EQ(b.onEdge(1100, true), E::EV_NONE);   // still down, not a new press
  b.onEdge(1200, false);
  EXPECT_EQ(settle(b, 1200 + BUTTON_CLICK_TIMEOUT_MS), E::EV_SHORT);
}

// ------------------------------------------------------------------- long press
TEST(Seq, LongPressFiresWhileHeld) {
  ButtonSequencer b; b.reset();
  b.onEdge(1000, true);
  EXPECT_EQ(b.tick(1000 + BUTTON_LONG_PRESS_TIME_MS), E::EV_LONG);
}
TEST(Seq, LongPressFiresOnlyOnce) {
  ButtonSequencer b; b.reset();
  b.onEdge(1000, true);
  EXPECT_EQ(b.tick(1000 + BUTTON_LONG_PRESS_TIME_MS),        E::EV_LONG);
  EXPECT_EQ(b.tick(1000 + BUTTON_LONG_PRESS_TIME_MS + 500),  E::EV_NONE);
}
// A long press must NOT also register as a click when released -- otherwise
// power-off would be followed by a stray SHORT.
TEST(Seq, LongPressReleaseDoesNotCountAsClick) {
  ButtonSequencer b; b.reset();
  b.onEdge(1000, true);
  EXPECT_EQ(b.tick(4000), E::EV_LONG);
  b.onEdge(4200, false);
  EXPECT_EQ(settle(b, 4200 + BUTTON_CLICK_TIMEOUT_MS), E::EV_NONE);
}
// The old code could fire a spurious LONG_PRESS after a MISSED release, which on the
// T1000-E means power-off. Here a release is always observed, so a held-then-released
// press below the threshold can never become a long press.
TEST(Seq, HeldJustUnderThresholdIsAClickNotALong) {
  ButtonSequencer b; b.reset();
  b.onEdge(1000, true);
  EXPECT_EQ(b.tick(1000 + BUTTON_LONG_PRESS_TIME_MS - 1), E::EV_NONE);
  b.onEdge(1000 + BUTTON_LONG_PRESS_TIME_MS - 1, false);
  EXPECT_EQ(settle(b, 1000 + BUTTON_LONG_PRESS_TIME_MS + BUTTON_CLICK_TIMEOUT_MS),
            E::EV_SHORT);
}

// ------------------------------------------------------------------- windows
TEST(Seq, GapBeyondWindowSplitsIntoTwoSingles) {
  ButtonSequencer b; b.reset();
  click(b, 1000);
  EXPECT_EQ(settle(b, 1060 + BUTTON_CLICK_TIMEOUT_MS), E::EV_SHORT);
  click(b, 2000);
  EXPECT_EQ(settle(b, 2060 + BUTTON_CLICK_TIMEOUT_MS), E::EV_SHORT);
}
TEST(Seq, NoEventBeforeWindowCloses) {
  ButtonSequencer b; b.reset();
  click(b, 1000); click(b, 1200);
  EXPECT_EQ(b.tick(1260 + BUTTON_CLICK_TIMEOUT_MS - 10), E::EV_NONE);  // still open
  EXPECT_EQ(b.tick(1260 + BUTTON_CLICK_TIMEOUT_MS),      E::EV_DOUBLE);
}
TEST(Seq, ResolvesOnlyOncePerGesture) {
  ButtonSequencer b; b.reset();
  click(b, 1000);
  EXPECT_EQ(settle(b, 1060 + BUTTON_CLICK_TIMEOUT_MS), E::EV_SHORT);
  EXPECT_EQ(settle(b, 1060 + BUTTON_CLICK_TIMEOUT_MS + 1000), E::EV_NONE);
}

// ------------------------------------------------------------------- safety
TEST(Seq, IdleProducesNothing) {
  ButtonSequencer b; b.reset();
  for (uint32_t t = 0; t < 10000; t += 250) EXPECT_EQ(b.tick(t), E::EV_NONE);
}
TEST(Seq, ResetClearsPendingGesture) {
  ButtonSequencer b; b.reset();
  click(b, 1000); click(b, 1200);
  b.reset();
  EXPECT_EQ(settle(b, 5000), E::EV_NONE);
}
TEST(Seq, MillisWraparound) {
  ButtonSequencer b; b.reset();
  const uint32_t near_wrap = 0xFFFFFF00u;
  b.onEdge(near_wrap, true);
  b.onEdge(near_wrap + 80, false);          // wraps past 0xFFFFFFFF
  EXPECT_EQ(settle(b, near_wrap + 80 + BUTTON_CLICK_TIMEOUT_MS), E::EV_SHORT);
}

int main(int argc, char **argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }
