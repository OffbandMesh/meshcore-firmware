// test/test_battery_gauge/test_battery_gauge.cpp -- Offband #1246
//
// The owner, on the bench: the badge bounces between 97% and 100% at full, and never
// shows 100%. These cover both halves of the answer -- endpoints a real cell reaches,
// and an average that a single noisy reading cannot move -- and the thing that would
// make the average a lie, which is hiding a battery that is genuinely going flat.
//
// Folder MUST stay `test_`-prefixed and this file provides its own main() -- this repo
// does not link gtest_main.

#include <gtest/gtest.h>
#include <cstdlib>
#include <tuple>
#include <vector>
#include "../../src/helpers/ui/BatteryGauge.h"

using namespace offband;

namespace {

// The badge's own ends. Empty is the voltage SafeBoot refuses to start from, not the one
// a running badge dies at (3400) -- there is reserve below 0%. Full is where a charged
// cell settles once the charger lets go.
constexpr uint16_t kEmpty = 3500;
constexpr uint16_t kFull = 4150;
constexpr uint16_t kDies = 3400;

int pct(uint16_t mv) { return batteryPercent(mv, kEmpty, kFull); }

// Pseudo-random noise rather than a balanced sawtooth, which would flatter any average by
// summing to nothing over its period. `bias` tilts it, to catch a filter that leans.
uint16_t settle(BatteryAverage& a, uint16_t mv, int wobble_mv, int readings, int bias = 0) {
  uint32_t s = 0x2545F491u;
  for (int i = 0; i < readings; i++) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;          // xorshift32
    const int noise = (int)(s % (uint32_t)(2 * wobble_mv + 1)) - wobble_mv + bias;
    a.feed((uint16_t)((int)mv + noise));
  }
  return a.value();
}

}  // namespace

// The symptom the owner reported: a charged cell reads 100, and holds it.
TEST(BatteryGauge, AFullCellReadsFull) {
  EXPECT_EQ(100, pct(4150));     // where a charged cell settles
  EXPECT_EQ(100, pct(4160));
  EXPECT_EQ(100, pct(4180));
  EXPECT_EQ(100, pct(4200));     // and on the charger, still 100 rather than over
}

// The other end, which was as wrong and less visible. 0% is the voltage SafeBoot refuses
// to start from: below it the badge will not come back, whatever is left in the cell.
TEST(BatteryGauge, EmptyIsWhereTheBadgeWillNotStart) {
  EXPECT_EQ(0, pct(3500));
  EXPECT_EQ(0, pct(kDies));       // the reserve it keeps running on, below 0%
  EXPECT_EQ(0, pct(0));
  // What the fleet's old straight line said at the same voltage -- 3000..4200, linear.
  EXPECT_EQ(42, (int)(((3500 - 3000) * 100 + 600) / 1200));
}

// The reserve is real and deliberate: the badge runs for another 100 mV past 0%. What it
// must never do is draw charge on a badge that cannot be turned back on.
//
// #1252: the bar now reads 0 up to about 3550 rather than 3500. That is the measurement,
// not a rounding artefact — at 3550 the burn had 0.6% of its charge left, and under one
// percent is zero on a bar. The badge still runs below it, which is the reserve above.
//
// ⚠ Stated as the limit it is: that 0.6% is the RC52 cell, at its age and temperature and
// drain. The badge's cell may empty a few millivolts either side of it. One sample is
// what we have, and it beats the straight line that was out by ten points in the middle.
TEST(BatteryGauge, NothingIsShownWhileThereIsNothingLeft) {
  for (uint16_t mv = kDies; mv <= 3550; mv += 10) {
    EXPECT_EQ(0, pct(mv)) << "mv " << mv;
  }
  EXPECT_GT(pct(3600), 0) << "and above the curve's floor the bar shows again";
}

TEST(BatteryGauge, NothingReachableLeavesTheRange) {
  for (uint16_t mv = 0; mv < 5000; mv += 1) {
    const int p = pct(mv);
    EXPECT_GE(p, 0) << "mv " << mv;
    EXPECT_LE(p, 100) << "mv " << mv;
  }
}

// #1252: the shape between the ends is the measured lithium discharge, not a straight
// line. These are the RC52 #1004 numbers, renormalised onto the badge's 3500..4150.
// Each one is about ten points above where a straight line put it.
TEST(BatteryGauge, TheMiddleFollowsTheMeasuredCurve) {
  EXPECT_NEAR(85, pct(4000), 2);    // a straight line said 77
  EXPECT_NEAR(65, pct(3850), 2);    //                      54
  EXPECT_NEAR(41, pct(3700), 2);    //                      31
  EXPECT_NEAR(15, pct(3600), 2);    //                      15 -- the two converge here
}

// A cell holds most of its charge in a narrow band, so the bar must fall unevenly: barely
// at the top, then quickly through the knee.
TEST(BatteryGauge, TheBarFallsFasterNearTheEndThanNearTheTop) {
  const int top = pct(4150) - pct(4050);        // the first 100 mV
  const int knee = pct(3650) - pct(3550);       // the last 100 mV before empty
  EXPECT_LT(top, knee) << "the curve must be steeper at the knee than at the top";
}

TEST(BatteryGauge, TheCurveNeverGoesBackwards) {
  int prev = -1;
  for (uint16_t mv = 3000; mv <= 4400; mv += 1) {
    const int p = pct(mv);
    EXPECT_GE(p, prev) << "mv " << mv << " went down as voltage went up";
    prev = p;
  }
}

// The table itself has to stay ordered, or the interpolation walks off it.
TEST(BatteryGauge, TheTableDescendsInBothColumns) {
  for (int i = 1; i < kLithiumCurvePoints; i++) {
    EXPECT_LT(kLithiumCurve[i].mv, kLithiumCurve[i - 1].mv) << "point " << i;
    EXPECT_LT(kLithiumCurve[i].permille, kLithiumCurve[i - 1].permille) << "point " << i;
  }
}

// Every breakpoint reads back as itself, and the curve holds flat beyond either end.
TEST(BatteryGauge, EveryBreakpointIsExactAndTheEndsHold) {
  for (int i = 0; i < kLithiumCurvePoints; i++) {
    EXPECT_EQ(kLithiumCurve[i].permille, chargeAt(kLithiumCurve[i].mv)) << "point " << i;
  }
  EXPECT_EQ(kLithiumCurve[0].permille, chargeAt(4400));
  EXPECT_EQ(kLithiumCurve[kLithiumCurvePoints - 1].permille, chargeAt(3000));
  // and halfway between two breakpoints is halfway between their values
  EXPECT_NEAR((953 + 901) / 2, chargeAt(4075), 1);   // between 4100 and 4050
  EXPECT_NEAR((414 + 382) / 2, chargeAt(3687), 2);   // and at the knee, where it matters
}

// #1252, the review's gap: ends that fall BETWEEN table points, where the interpolation
// and the renormalisation both have to be right at once. Landing on a breakpoint hides
// arithmetic that lands between them does not.
TEST(BatteryGauge, EndsBetweenBreakpointsStillScaleCleanly) {
  const uint16_t lo = 3735, hi = 4085;          // neither is in the table
  EXPECT_EQ(0, batteryPercent(lo, lo, hi));
  EXPECT_EQ(100, batteryPercent(hi, lo, hi));
  int prev = -1;
  for (uint16_t mv = lo; mv <= hi; mv += 1) {
    const int p = batteryPercent(mv, lo, hi);
    EXPECT_GE(p, prev) << "mv " << mv;
    EXPECT_GE(p, 0);
    EXPECT_LE(p, 100);
    prev = p;
  }
}

// A board whose whole range sits outside what the burn covered cannot be read off the
// curve -- both ends clamp to the same value. It falls back to the straight line rather
// than drawing an empty bar across everything that board can do.
TEST(BatteryGauge, ABoardTheCurveCannotSeeFallsBackToTheLine) {
  const uint16_t lo = 3000, hi = 3540;          // entirely below the curve's floor
  EXPECT_EQ(0, batteryPercent(lo, lo, hi));
  EXPECT_EQ(100, batteryPercent(hi, lo, hi));
  EXPECT_EQ(50, batteryPercent(3270, lo, hi));  // the midpoint, linearly
  EXPECT_GT(batteryPercent(3400, lo, hi), 0) << "the range must not read empty throughout";
}

// The curve supplies the shape; the board supplies where it starts and stops. Any board's
// own ends still read exactly 0 and 100.
TEST(BatteryGauge, AnyBoardsOwnEndsStillReadZeroAndFull) {
  const uint16_t ends[][2] = {{3500, 4150}, {3400, 4200}, {3000, 4200}, {3600, 4100}};
  for (const auto& e : ends) {
    SCOPED_TRACE(testing::Message() << e[0] << ".." << e[1]);
    EXPECT_EQ(0, batteryPercent(e[0], e[0], e[1]));
    EXPECT_EQ(100, batteryPercent(e[1], e[0], e[1]));
    EXPECT_GT(batteryPercent((uint16_t)((e[0] + e[1]) / 2), e[0], e[1]), 0);
    EXPECT_LT(batteryPercent((uint16_t)((e[0] + e[1]) / 2), e[0], e[1]), 100);
  }
}

TEST(BatteryGauge, ANonsenseRangeReadsEmptyRatherThanDividingByZero) {
  EXPECT_EQ(0, batteryPercent(4000, 4200, 4200));
  EXPECT_EQ(0, batteryPercent(4000, 4200, 3000));
}

// The first reading is taken whole, so the bar is right at boot instead of climbing to
// the truth over the first quarter minute.
TEST(BatteryAverageTest, TheFirstReadingIsTakenWhole) {
  BatteryAverage a;
  EXPECT_FALSE(a.seeded());
  EXPECT_EQ(0, a.value());
  a.feed(4100);
  EXPECT_TRUE(a.seeded());
  EXPECT_EQ(4100, a.value());
}

// The cause of the bounce: the number was whatever the ADC said at the instant of a
// redraw. A wobble of this size is what the bench showed -- 97% to 100% on the old
// scale, which was 36 mV.
TEST(BatteryAverageTest, NoiseThatMovedThePercentageNoLongerDoes) {
  BatteryAverage a;
  const uint16_t v = settle(a, 4160, 18, 60);
  EXPECT_NEAR(4160, v, 4);                 // within a few mV of the truth
  // and on the badge's scale that is a single percentage, unmoving
  EXPECT_EQ(100, pct(v));

  BatteryAverage mid;
  const uint16_t m = settle(mid, 3800, 18, 60);
  EXPECT_NEAR(3800, m, 4);
  EXPECT_EQ(pct(3800), pct(m));
}

// The test the issue asked for: the average must not hide a battery going flat. A cell
// falling at a realistic rate is tracked, and one that falls off a cliff is caught in
// seconds, not minutes.
TEST(BatteryAverageTest, ARealDischargeStillComesThrough) {
  BatteryAverage a;
  a.feed(4100);
  // a slow fall: 1 mV a second is about a 12-hour discharge across this cell's range
  uint16_t mv = 4100;
  for (int s = 0; s < 600; s++) {
    mv = (uint16_t)(4100 - s);
    a.feed(mv);
  }
  EXPECT_NEAR(mv, a.value(), 20) << "a slow fall must be tracked, not averaged away";

  // a cliff: the supply drops 300 mV and stays there. The dip guard holds it off for a
  // few readings and then believes it, and the average closes the rest of the way.
  BatteryAverage b;
  b.feed(4000);
  for (int s = 0; s < 120; s++) b.feed(3700);
  EXPECT_NEAR(3700, b.value(), 10) << "a sustained drop must arrive, not be averaged away";
  EXPECT_EQ(pct(3700), pct(b.value()));
}

// A transmit pulls over a hundred milliamps and sags the supply for as long as it lasts.
// That is not the charge falling, and it must not move the number at all.
TEST(BatteryAverageTest, OneDipDoesNotMoveItAtAll) {
  BatteryAverage a;
  for (int i = 0; i < 40; i++) a.feed(4000);
  const uint16_t before = a.value();
  a.feed(3850);                            // one reading taken during a transmit
  EXPECT_EQ(before, a.value());
  EXPECT_EQ(pct(before), pct(a.value()));
  a.feed(4000);                            // the transmit ends
  EXPECT_EQ(before, a.value());
}

// Two in a row is still a transmit; three is the cell.
TEST(BatteryAverageTest, ADipIsBelievedOnceItKeepsHappening) {
  BatteryAverage a;
  for (int i = 0; i < 40; i++) a.feed(4000);
  const uint16_t before = a.value();
  a.feed(3850);
  a.feed(3850);
  EXPECT_EQ(before, a.value());
  a.feed(3850);
  EXPECT_LT(a.value(), before) << "a third reading in a row is not a transmit";
}

// Only a fall is suspect. A rise is a charger being plugged in, and is taken as it comes.
TEST(BatteryAverageTest, ARiseIsNeverHeldOff) {
  BatteryAverage a;
  for (int i = 0; i < 40; i++) a.feed(3800);
  const uint16_t before = a.value();
  a.feed(4200);
  EXPECT_GT(a.value(), before);
}

// Guarding only falls could lean the average upwards, because a high sample is taken and
// a low one is not. It does not, because the threshold is far outside the noise: at the
// spread the bench showed, and at three times it, the average still lands on the truth.
TEST(BatteryAverageTest, GuardingDipsDoesNotLeanTheAverageUp) {
  BatteryAverage a;
  a.feed(4000);
  EXPECT_NEAR(4000, settle(a, 4000, 18, 300), 5);

  BatteryAverage b;
  b.feed(4000);
  EXPECT_NEAR(4000, settle(b, 4000, 55, 300), 12) << "still centred at three times the spread";
}

// A filter that leaned would show here, where the readings themselves lean.
TEST(BatteryAverageTest, ALopsidedReadingIsFollowed) {
  BatteryAverage a;
  a.feed(4000);
  EXPECT_NEAR(4030, settle(a, 4000, 18, 300, 30), 6);
  BatteryAverage b;
  b.feed(4000);
  EXPECT_NEAR(3970, settle(b, 4000, 18, 300, -30), 6);
}

// The review's case: boot during a transmit and the average is seeded low. It must climb
// out at once, not crawl for a quarter of a minute showing a false low battery.
TEST(BatteryAverageTest, ASeedTakenDuringATransmitIsCorrectedAtOnce) {
  BatteryAverage a;
  a.feed(3850);              // the first reading landed under load
  EXPECT_EQ(3850, a.value());
  a.feed(4000);              // the transmit ends
  EXPECT_EQ(4000, a.value()) << "a large rise is taken whole, not crawled to";
}

// The review's other case: a transmit every other second. The loaded readings are held
// off and the unloaded ones are believed, so the average tracks the cell and not the
// radio -- which is the point, not a way around the counter.
TEST(BatteryAverageTest, ATransmitEveryOtherSecondTracksTheCellNotTheRadio) {
  BatteryAverage a;
  a.feed(4000);
  for (int i = 0; i < 60; i++) a.feed(i % 2 ? 3850 : 4000);
  EXPECT_EQ(4000, a.value());

  // and when the cell itself falls under that pattern, the unloaded readings carry it --
  // trailing the last of them by about the lag of an average that only sees every other
  // second, and nowhere near the 3700 the radio keeps pulling it to.
  BatteryAverage b;
  b.feed(4000);
  for (int i = 0; i < 200; i++) b.feed(i % 2 ? 3700 : (uint16_t)(4000 - i / 2));
  const uint16_t last_unloaded = 4000 - 198 / 2;      // 3901
  EXPECT_GT(b.value(), last_unloaded);
  EXPECT_LT(b.value() - last_unloaded, 20);
  EXPECT_GT(b.value(), 3800) << "the radio must not drag the reading down with it";
}

TEST(BatteryAverageTest, ResetForgetsEverything) {
  BatteryAverage a;
  a.feed(4000);
  a.reset();
  EXPECT_FALSE(a.seeded());
  a.feed(3500);
  EXPECT_EQ(3500, a.value());
}

// ---- #1254: learning where 100% is --------------------------------------------------

namespace {
constexpr uint32_t kMin = 60UL * 1000UL;

// Run the learner over a script of (minutes, mv, external) and return what it learned.
uint16_t learn(FullPointLearner& l, const std::vector<std::tuple<int, uint16_t, bool>>& s) {
  uint16_t got = 0;
  uint32_t t = 0;
  for (const auto& step : s) {
    for (int m = 0; m < std::get<0>(step); m++) {
      t += kMin;
      const uint16_t r = l.feed(t, std::get<1>(step), std::get<2>(step));
      if (r) got = r;
    }
  }
  return got;
}
}  // namespace

// The bench case: charged to full, then unplugged.
TEST(FullPoint, AFullChargeThenUnplugTeachesTheRestedVoltage) {
  FullPointLearner l;
  EXPECT_EQ(4148, learn(l, {{20, 4183, true}, {10, 4148, false}}));
}

// The charger holds the cell above where it rests -- 4183 against 4148 on the owner's
// badge. What gets learned is the battery-side value, never the charger's.
TEST(FullPoint, TheChargersOwnVoltageIsNeverWhatIsLearned) {
  FullPointLearner l;
  const uint16_t got = learn(l, {{20, 4183, true}, {10, 4148, false}});
  EXPECT_LT(got, 4183);
  EXPECT_EQ(4148, got);
}

// Unplugged half-charged: the charge never reached a full-charge voltage, so nothing is
// learned and the board's own default still stands.
TEST(FullPoint, APartialChargeTeachesNothing) {
  FullPointLearner l;
  EXPECT_EQ(0, learn(l, {{20, 3900, true}, {10, 3880, false}}));
}

// The real unplug trace from the bench: the reading swings 20 mV with the radio. Taking
// the maximum is what makes that harmless -- a timed sample would have caught 4086.
TEST(FullPoint, LoadSwingsAfterUnpluggingDoNotDragItDown) {
  FullPointLearner l;
  uint32_t t = 0;
  for (int m = 0; m < 20; m++) { t += kMin; l.feed(t, 4160, true); }
  uint16_t got = 0;
  for (uint16_t mv : {4106, 4086, 4096, 4088, 4101, 4106, 4096, 4102}) {
    t += kMin;
    const uint16_t r = l.feed(t, mv, false);
    if (r) got = r;
  }
  EXPECT_EQ(4106, got) << "the high-water mark, not whatever the radio was doing";
}

TEST(FullPoint, NothingImplausibleIsEverLearned) {
  FullPointLearner a, b;
  EXPECT_EQ(0, learn(a, {{20, 4150, true}, {10, 3500, false}}));   // below the floor
  EXPECT_EQ(0, learn(b, {{20, 4150, true}, {10, 4600, false}}));   // above the ceiling
}

// Running on battery all along, never charged: there is nothing to learn from.
TEST(FullPoint, BatteryOnlyOperationLearnsNothing) {
  FullPointLearner l;
  EXPECT_EQ(0, learn(l, {{60, 4100, false}}));
}

// Plugged back in before the window closes: that charge is not finished, so it waits for
// the next unplug rather than learning a half-window maximum.
TEST(FullPoint, PluggingBackInAbandonsTheWindow) {
  FullPointLearner l;
  EXPECT_EQ(0, learn(l, {{20, 4183, true}, {2, 4148, false}, {5, 4183, true}}));
  // and the next full unplug still works
  EXPECT_EQ(4150, learn(l, {{10, 4150, false}}));
}

// The review read the within-window maximum as the across-life behaviour and concluded a
// sagging cell could never be followed. It can: every charge that passes the gate starts
// a fresh window, so the value moves down as readily as up.
TEST(FullPoint, AnAgeingCellIsFollowedDown) {
  FullPointLearner l;
  EXPECT_EQ(4180, learn(l, {{20, 4200, true}, {10, 4180, false}}));   // new cell
  EXPECT_EQ(4120, learn(l, {{20, 4190, true}, {10, 4120, false}}));   // a year later
  EXPECT_EQ(3980, learn(l, {{20, 4150, true}, {10, 3980, false}}));   // tired
}

// A charge that stops just under the gate teaches nothing; just over it teaches.
TEST(FullPoint, TheGateIsWhereItSays) {
  FullPointLearner a, b;
  EXPECT_EQ(0, learn(a, {{20, 4099, true}, {10, 4090, false}}));
  EXPECT_EQ(4100, learn(b, {{20, 4100, true}, {10, 4100, false}}));
}

// Deadlines are compared by difference, so a window that spans a millis() wrap still ends.
TEST(FullPoint, AWindowSurvivesAMillisWrap) {
  FullPointLearner l;
  uint32_t t = 0xFFFF0000UL;                       // ~4.3 s before the wrap
  for (int m = 0; m < 5; m++) { t += 1000; l.feed(t, 4183, true); }
  uint16_t got = 0;
  for (int m = 0; m < 400; m++) {                  // straight through 0
    t += 1000;
    const uint16_t r = l.feed(t, 4148, false);
    if (r) got = r;
  }
  EXPECT_EQ(4148, got);
}

// End to end, in the numbers the badge actually sees. The learner is fed the board's own
// ADC, which reads about 22 mV below the rig's gauge at rest -- so the owner's cell, 4183
// charging and 4148 rested on the gauge, arrives here as roughly 4161 and 4126. That
// difference is the whole reason the badge must learn its OWN number rather than be told
// the cell's.
TEST(FullPoint, WhatIsLearnedIsWhatReadsAsFull) {
  FullPointLearner l;
  const uint16_t full = learn(l, {{20, 4161, true}, {10, 4126, false}});
  ASSERT_EQ(4126, full);
  EXPECT_EQ(100, batteryPercent(full, kEmpty, full));
  EXPECT_EQ(100, batteryPercent(4140, kEmpty, full));   // and anything above it
  EXPECT_LT(batteryPercent(4000, kEmpty, full), 100);
  // This is the defect it fixes: against the compiled 4150 the same reading stops short.
  EXPECT_EQ(98, batteryPercent(4126, kEmpty, 4150));
}

// A press at the wrong moment cannot pin something that breaks the bar.
TEST(FullPoint, OnlyAPlausibleValueCanBePinned) {
  EXPECT_TRUE(FullPointLearner::plausibleFullMv(4148));
  EXPECT_TRUE(FullPointLearner::plausibleFullMv(3900));
  EXPECT_TRUE(FullPointLearner::plausibleFullMv(4250));
  EXPECT_FALSE(FullPointLearner::plausibleFullMv(3400));   // below the badge's empty
  EXPECT_FALSE(FullPointLearner::plausibleFullMv(3899));
  EXPECT_FALSE(FullPointLearner::plausibleFullMv(4251));
  EXPECT_FALSE(FullPointLearner::plausibleFullMv(0));
}

// #1254: the Battery screen says where the number came from, so a mistimed press is
// visible rather than silent. Nothing stored means the board's compiled value is in use.
TEST(FullPoint, TheScreenSaysWhereTheNumberCameFrom) {
  EXPECT_STREQ("default", fullPointSourceName(0, false));
  EXPECT_STREQ("default", fullPointSourceName(0, true));    // stored nothing, so nothing is set
  EXPECT_STREQ("learned", fullPointSourceName(4126, false));
  EXPECT_STREQ("set", fullPointSourceName(4126, true));
}

// The events are what a bench cycle is read by, so each must fire at the step it names
// and nowhere else. This walks tonight's bench case: on USB at a reading below the gate,
// then unplugged -- which must say "gate failed", with the number, and learn nothing.
TEST(FullPoint, EachEventFiresAtTheStepItNames) {
  FullPointLearner l;
  uint32_t t = 0;
  auto step = [&](uint16_t mv, bool ext) { t += kMin; return l.feed(t, mv, ext); };

  step(4073, true);
  EXPECT_EQ(FullPointLearner::kPluggedIn, l.event());   // USB seen
  step(4073, true);
  EXPECT_EQ(FullPointLearner::kNone, l.event());        // still USB: nothing new

  step(4040, false);
  EXPECT_EQ(FullPointLearner::kGateFailed, l.event());  // unplugged below the gate
  EXPECT_EQ(4073, l.chargeMv());                        // and the number it failed on
  for (int m = 0; m < 10; m++) EXPECT_EQ(0, step(4040, false));
  EXPECT_EQ(FullPointLearner::kNone, l.event());        // not watching, so silent

  // A real top-off this time.
  step(4121, true);
  EXPECT_EQ(FullPointLearner::kPluggedIn, l.event());
  step(4121, false);
  EXPECT_EQ(FullPointLearner::kGatePassed, l.event());
  uint16_t got = 0;
  for (int m = 0; m < 10 && !got; m++) got = step(4121, false);
  EXPECT_EQ(4121, got);
  EXPECT_EQ(FullPointLearner::kLearned, l.event());
}

// The window opens on the unplug and closes on the first feed at or after kWatchMs; that
// closing feed, and only that one, reports. Fed a minute apart, the unplug is minute 0 and
// the close is minute 5.
TEST(FullPoint, AnImplausibleWindowReportsOnTheClosingFeedOnly) {
  FullPointLearner l;
  uint32_t t = 0;
  auto step = [&](uint16_t mv, bool ext) { t += kMin; return l.feed(t, mv, ext); };
  step(4150, true);
  step(3500, false);
  EXPECT_EQ(FullPointLearner::kGatePassed, l.event());
  for (int m = 1; m < 5; m++) {
    step(3500, false);
    EXPECT_EQ(FullPointLearner::kNone, l.event()) << "minute " << m << " is still inside the window";
  }
  EXPECT_EQ(0, step(3500, false));
  EXPECT_EQ(FullPointLearner::kImplausible, l.event()) << "minute 5 closes it";
  EXPECT_EQ(3500, l.bestMv());
  step(3500, false);
  EXPECT_EQ(FullPointLearner::kNone, l.event()) << "and it says so once";
}

// What a learn reports is what it measured: bestMv at the closing feed is the value
// returned, so the log line and the stored value cannot disagree.
TEST(FullPoint, ALearnReportsTheValueItReturns) {
  FullPointLearner l;
  uint32_t t = 0;
  auto step = [&](uint16_t mv, bool ext) { t += kMin; return l.feed(t, mv, ext); };
  step(4161, true);
  step(4110, false);
  step(4126, false);
  step(4104, false);
  step(4119, false);
  step(4101, false);
  const uint16_t got = step(4100, false);
  EXPECT_EQ(FullPointLearner::kLearned, l.event());
  EXPECT_EQ(4126, got);
  EXPECT_EQ(got, l.bestMv());
}

TEST(FullPoint, ResetForgetsAPendingWindow) {
  FullPointLearner l;
  uint32_t t = 0;
  for (int m = 0; m < 20; m++) { t += kMin; l.feed(t, 4183, true); }
  t += kMin; l.feed(t, 4148, false);
  l.reset();
  for (int m = 0; m < 10; m++) { t += kMin; EXPECT_EQ(0, l.feed(t, 4148, false)); }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
