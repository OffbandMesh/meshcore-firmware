// #527: press-counting state machine tests.
//
// The defect this replaces: a triple-press resolved as a SINGLE on real hardware,
// because presses that were not sampled at the right instant were discarded with no
// counter increment, no event and no log. These tests exist so that failure mode is
// impossible to reintroduce silently -- they feed edges with explicit timestamps, the
// way a real burst arrives, rather than depending on when a loop happened to look.
//
// The FIELD-CAPTURED gestures at the bottom come from the owner's own presses off the
// device (debug_logs/serial-capture-20260802-132132.txt). He taps fast -- well under
// 50 ms between presses -- and that is the requirement, not an edge case.
#include <gtest/gtest.h>
#include <vector>
#include "../../examples/companion_radio/ui-orig/ButtonSequencer.h"

using E = ButtonSequencer::Event;

// Mirrors Button::update() exactly: tick to each edge's own timestamp BEFORE feeding
// it, so a deferred edge stays ahead of the edge behind it, and drain fully afterwards.
// Testing against anything looser would not be testing what ships.
struct Harness {
  ButtonSequencer b;
  std::vector<E> all;

  Harness() { b.reset(); }

  void drain(uint32_t at) {
    for (;;) {
      E e = b.tick(at);
      if (e == E::EV_NONE) return;
      all.push_back(e);
    }
  }
  void edge(uint32_t ms, bool pressed) {
    drain(ms);
    E e = b.onEdge(ms, pressed);
    if (e != E::EV_NONE) all.push_back(e);
  }
  // press down at `t`, release after `hold`
  void tap(uint32_t t, uint32_t hold) { edge(t, true); edge(t + hold, false); }

  // Resolved gestures only -- press-edge notifications are display housekeeping.
  std::vector<E> gestures() const {
    std::vector<E> g;
    for (E e : all) if (e != E::EV_PRESS_EDGE) g.push_back(e);
    return g;
  }
  int pressEdges() const {
    int n = 0;
    for (E e : all) if (e == E::EV_PRESS_EDGE) n++;
    return n;
  }
};

static std::vector<E> one(E e) { return std::vector<E>{e}; }

// ------------------------------------------------------------------ basic counts
TEST(Seq, SingleClick) {
  Harness h; h.tap(1000, 60); h.drain(1060 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_SHORT));
}
TEST(Seq, DoubleClick) {
  Harness h; h.tap(1000, 60); h.tap(1200, 60); h.drain(1260 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_DOUBLE));
}
TEST(Seq, TripleClick) {
  Harness h; h.tap(1000, 60); h.tap(1200, 60); h.tap(1400, 60);
  h.drain(1460 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_TRIPLE));
}
TEST(Seq, QuadrupleClick) {
  Harness h;
  for (int i = 0; i < 4; i++) h.tap(1000 + i * 150, 60);
  h.drain(1510 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_QUADRUPLE));
}
TEST(Seq, FiveClicksClampToQuadruple) {
  Harness h;
  for (int i = 0; i < 5; i++) h.tap(1000 + i * 150, 60);
  h.drain(2000 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_QUADRUPLE));
}

// ------------------------------------------------- THE FIELD FAILURE #1: a late loop
// Four taps in under a second, then the consumer does not drain for 800 ms because the
// main loop was busy. The old implementation lost three of these outright. The count
// must survive, because the edges carry their own timestamps.
TEST(Seq, LateDrainStillResolvesQuadruple) {
  Harness h;
  for (int i = 0; i < 4; i++) h.tap(1000 + i * 160, 80);
  h.drain(1560 + 800);            // nothing drained for 800ms after the last release
  EXPECT_EQ(h.gestures(), one(E::EV_QUADRUPLE));
}
TEST(Seq, EdgesDeliveredInOneBatchAfterStall) {
  Harness h;
  h.tap(2000, 70); h.tap(2200, 70); h.tap(2400, 70);
  h.drain(3500);
  EXPECT_EQ(h.gestures(), one(E::EV_TRIPLE));
}

// ------------------------------------------- THE FIELD FAILURE #2: fast taps (#527)
// The owner taps bam-bam-bam, well under 50 ms between presses. The first version of
// this file DISCARDED any edge inside the debounce window, which erased the first
// release, made the second press stop looking like a transition, and collapsed the
// whole gesture into one click. These are the gestures that must work.
TEST(Seq, FastDoubleTap_30msPress_30msGap) {
  Harness h;
  h.tap(1000, 30);      // down 1000, up 1030
  h.tap(1060, 30);      // down 1060, up 1090
  h.drain(1090 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_DOUBLE));
  EXPECT_EQ(h.pressEdges(), 2);
}
TEST(Seq, FastTripleTap_UnderFiftyMsGaps) {
  Harness h;
  h.tap(1000, 35); h.tap(1070, 35); h.tap(1140, 35);
  h.drain(1175 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_TRIPLE));
  EXPECT_EQ(h.pressEdges(), 3);
}
TEST(Seq, FastQuadTap_WholeGestureUnder500ms) {
  Harness h;
  for (int i = 0; i < 4; i++) h.tap(1000 + i * 110, 40);   // whole gesture ~370ms
  h.drain(1370 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_QUADRUPLE));
  EXPECT_EQ(h.pressEdges(), 4);
}
// The narrowest gesture the 10 ms window can carry: press and gap both at the floor.
TEST(Seq, TapsAtTheDebounceFloorStillCount) {
  Harness h;
  h.tap(1000, BUTTON_DEBOUNCE_TIME_MS);
  h.tap(1000 + 2 * BUTTON_DEBOUNCE_TIME_MS, BUTTON_DEBOUNCE_TIME_MS);
  h.drain(2000);
  EXPECT_EQ(h.gestures(), one(E::EV_DOUBLE));
}

// ------------------------------------------------------------------ press edges
TEST(Seq, PressEdgeReportedPerPress) {
  Harness h;
  h.edge(1000, true); h.edge(1060, false); h.edge(1200, true);
  EXPECT_EQ(h.pressEdges(), 2);
}

// ------------------------------------------------------------------ debounce
// Contact chatter: make/break several times in a few ms. Must be ONE click, because
// the deferred level is overwritten each time and only what the contact settles on is
// ever applied.
TEST(Seq, BounceBurstOnPressCountsOnce) {
  Harness h;
  h.edge(1000, true);
  h.edge(1002, false); h.edge(1004, true); h.edge(1006, false); h.edge(1008, true);
  h.edge(1200, false);            // real release, well clear
  h.drain(1200 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_SHORT));
  EXPECT_EQ(h.pressEdges(), 1);
}
TEST(Seq, BounceBurstOnReleaseCountsOnce) {
  Harness h;
  h.edge(1000, true);
  h.edge(1100, false);
  h.edge(1102, true); h.edge(1104, false); h.edge(1106, true); h.edge(1108, false);
  h.drain(1200 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_SHORT));
}
TEST(Seq, RepeatedSameLevelIsNotATransition) {
  Harness h;
  h.edge(1000, true);
  h.edge(1100, true);             // still down, not a new press
  h.edge(1200, false);
  h.drain(1200 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_SHORT));
  EXPECT_EQ(h.pressEdges(), 1);
}

// ------------------------------------------------------- deferral, not deletion
// A tap SHORTER than the debounce window. Under the old discard rule the release was
// thrown away, the machine went on believing the button was held, and fired a LONG
// press 3 s later -- power-off on the T1000-E. It must resolve as a click, and there
// must be no LONG afterwards.
TEST(Seq, TapShorterThanDebounceStillCountsAndNeverWedges) {
  Harness h;
  h.edge(1000, true);
  h.edge(1000 + BUTTON_DEBOUNCE_TIME_MS - 5, false);   // inside the window
  h.drain(1000 + BUTTON_DEBOUNCE_TIME_MS + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_SHORT));
  EXPECT_FALSE(h.b.isDown());
  h.drain(1000 + 5000);                                // long past the long-press mark
  EXPECT_EQ(h.gestures(), one(E::EV_SHORT));           // still exactly one event
}
TEST(Seq, DeferredEdgeAppliesEvenWithNoFurtherInput) {
  Harness h;
  h.edge(1000, true);
  h.edge(1003, false);
  EXPECT_TRUE(h.b.isDown());                           // not applied yet
  h.drain(1000 + BUTTON_DEBOUNCE_TIME_MS);
  EXPECT_FALSE(h.b.isDown());                          // window closed, release applied
}

// ------------------------------------------------------------------- long press
TEST(Seq, LongPressFiresWhileHeld) {
  Harness h;
  h.edge(1000, true);
  h.drain(1000 + BUTTON_LONG_PRESS_TIME_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_LONG));
}
TEST(Seq, LongPressFiresOnlyOnce) {
  Harness h;
  h.edge(1000, true);
  h.drain(1000 + BUTTON_LONG_PRESS_TIME_MS);
  h.drain(1000 + BUTTON_LONG_PRESS_TIME_MS + 500);
  EXPECT_EQ(h.gestures(), one(E::EV_LONG));
}
// A long press must NOT also register as a click when released -- otherwise power-off
// would be followed by a stray SHORT.
TEST(Seq, LongPressReleaseDoesNotCountAsClick) {
  Harness h;
  h.edge(1000, true);
  h.drain(4000);
  h.edge(4200, false);
  h.drain(4200 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_LONG));
}
TEST(Seq, HeldJustUnderThresholdIsAClickNotALong) {
  Harness h;
  h.edge(1000, true);
  h.edge(1000 + BUTTON_LONG_PRESS_TIME_MS - 1, false);
  h.drain(1000 + BUTTON_LONG_PRESS_TIME_MS + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_SHORT));
}

// ------------------------------------------------------------------- windows
TEST(Seq, GapBeyondWindowSplitsIntoTwoSingles) {
  Harness h;
  h.tap(1000, 60); h.drain(1060 + BUTTON_CLICK_TIMEOUT_MS);
  h.tap(2000, 60); h.drain(2060 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), (std::vector<E>{E::EV_SHORT, E::EV_SHORT}));
}
TEST(Seq, NoEventBeforeWindowCloses) {
  Harness h;
  h.tap(1000, 60); h.tap(1200, 60);
  h.drain(1260 + BUTTON_CLICK_TIMEOUT_MS - 10);
  EXPECT_TRUE(h.gestures().empty());
  h.drain(1260 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_DOUBLE));
}
TEST(Seq, ResolvesOnlyOncePerGesture) {
  Harness h;
  h.tap(1000, 60);
  h.drain(1060 + BUTTON_CLICK_TIMEOUT_MS);
  h.drain(1060 + BUTTON_CLICK_TIMEOUT_MS + 1000);
  EXPECT_EQ(h.gestures(), one(E::EV_SHORT));
}

// ------------------------------------------------------------------- safety
TEST(Seq, IdleProducesNothing) {
  Harness h;
  for (uint32_t t = 0; t < 10000; t += 250) h.drain(t);
  EXPECT_TRUE(h.all.empty());
}
TEST(Seq, ResetClearsPendingGesture) {
  Harness h;
  h.tap(1000, 60); h.tap(1200, 60);
  h.b.reset(); h.all.clear();
  h.drain(5000);
  EXPECT_TRUE(h.all.empty());
}
TEST(Seq, MillisWraparound) {
  Harness h;
  const uint32_t near_wrap = 0xFFFFFF00u;
  h.tap(near_wrap, 80);                               // release wraps past 0xFFFFFFFF
  h.drain(near_wrap + 80 + BUTTON_CLICK_TIMEOUT_MS);
  EXPECT_EQ(h.gestures(), one(E::EV_SHORT));
}

// -------------------------------------------------------- field-captured gestures
// Real press-down timestamps from the owner's device. The triples were already being
// counted correctly and must stay that way.
TEST(Seq, FieldCapture_TripleAt95297) {
  Harness h;
  h.tap(95297, 300); h.tap(95780, 300); h.tap(96224, 379);
  h.drain(97103);
  EXPECT_EQ(h.gestures(), one(E::EV_TRIPLE));
}
TEST(Seq, FieldCapture_TripleAt111033) {
  Harness h;
  h.tap(111033, 250); h.tap(111393, 250); h.tap(111869, 307);
  h.drain(112676);
  EXPECT_EQ(h.gestures(), one(E::EV_TRIPLE));
}

int main(int argc, char **argv) { ::testing::InitGoogleTest(&argc, argv); return RUN_ALL_TESTS(); }
