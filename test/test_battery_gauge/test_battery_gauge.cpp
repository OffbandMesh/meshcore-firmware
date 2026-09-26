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
constexpr uint32_t kSec = 1000UL;

// UITask feeds the learner once a second, and the learner counts readings as well as
// elapsed time, so the tests feed it the same way. Scripts stay in minutes.
uint16_t run(FullPointLearner& l, uint32_t& t, int minutes, uint16_t mv, bool external) {
  uint16_t got = 0;
  for (int i = 0; i < minutes * 60; i++) {
    t += kSec;
    const uint16_t r = l.feed(t, mv, external);
    if (r) got = r;
  }
  return got;
}

// Same, reporting the last decision the learner raised: only the feed that decides
// something raises an event, and at a reading a second most feeds decide nothing.
FullPointLearner::Event runEvent(FullPointLearner& l, uint32_t& t, int minutes, uint16_t mv,
                                bool external) {
  FullPointLearner::Event last = FullPointLearner::kNone;
  for (int i = 0; i < minutes * 60; i++) {
    t += kSec;
    l.feed(t, mv, external);
    if (l.event() != FullPointLearner::kNone) last = l.event();
  }
  return last;
}

// Run the learner over a script of (minutes, mv, external) and return what it learned.
uint16_t learn(FullPointLearner& l, const std::vector<std::tuple<int, uint16_t, bool>>& s) {
  uint16_t got = 0;
  uint32_t t = 0;
  for (const auto& step : s) {
    const uint16_t r = run(l, t, std::get<0>(step), std::get<1>(step), std::get<2>(step));
    if (r) got = r;
  }
  return got;
}
}  // namespace

// The bench case: left on USB until the charger stopped adding, then unplugged. The half
// hour is the measured window; 35 minutes is one closed window and a little.
TEST(FullPoint, ASettledChargeThenUnplugTeachesTheRestedVoltage) {
  FullPointLearner l;
  EXPECT_EQ(4148, learn(l, {{35, 4183, true}, {10, 4148, false}}));
}

// The charger holds the cell above where it rests -- 4183 against 4148 on the owner's
// badge -- and the average carries that higher number for the first seconds on battery.
// What gets learned is the battery-side value, never the charger's.
TEST(FullPoint, TheChargersOwnVoltageIsNeverWhatIsLearned) {
  FullPointLearner l;
  uint32_t t = 0;
  run(l, t, 35, 4183, true);
  t += kSec;
  l.feed(t, 4183, false);                           // unplug: the average still says 4183
  for (int s = 1; s <= 59; s++) { t += kSec; l.feed(t, 4180, false); }   // still catching up
  uint16_t got = 0;
  for (int s = 60; s <= 300; s++) {
    t += kSec;
    const uint16_t r = l.feed(t, 4148, false);
    if (r) got = r;
  }
  EXPECT_LT(got, 4183);
  EXPECT_EQ(4148, got);
}

// The settle counts readings as well as time, so a main loop running far below a reading
// a second cannot slip the charger's voltage through: the window closes having sampled
// nothing, and says so.
TEST(FullPoint, ASlowLoopLearnsNothingRatherThanTheChargersVoltage) {
  FullPointLearner l;
  uint32_t t = 0;
  run(l, t, 35, 4183, true);
  t += kSec;
  l.feed(t, 4183, false);                           // unplug
  uint16_t got = 0;
  for (int i = 0; i < 10; i++) {                    // a reading every 30 s, not every second
    t += 30UL * kSec;                               // the tenth is five minutes in
    const uint16_t r = l.feed(t, 4150, false);
    if (r) got = r;
  }
  EXPECT_EQ(0, got);
  EXPECT_EQ(FullPointLearner::kImplausible, l.event()) << "and the log says why";
  EXPECT_EQ(0, l.bestMv());
}

// Unplugged while the charger is still working. The capture measured that at about
// 0.7 mV a minute, so a half hour of it clears kFlatRiseMv several times over and the
// badge learns nothing from that unplug.
TEST(FullPoint, AChargeStillRunningTeachesNothing) {
  FullPointLearner l;
  uint32_t t = 0;
  FullPointLearner::Event last = FullPointLearner::kNone;
  for (int m = 0; m < 31; m++) last = runEvent(l, t, 1, (uint16_t)(4000 + m), true);
  EXPECT_EQ(FullPointLearner::kStillCharging, last) << "the half hour fills on minute 31";
  EXPECT_FALSE(l.chargerDone());
  for (int m = 31; m < 40; m++) runEvent(l, t, 1, (uint16_t)(4000 + m), true);
  t += kSec;
  l.feed(t, 4030, false);
  EXPECT_EQ(FullPointLearner::kNotLearning, l.event());
  EXPECT_EQ(0, run(l, t, 10, 4030, false));
}

// A charger that stops early is still this badge's 100%: the rule is "the charger has
// stopped adding", not "the voltage is high". The owner's cell reads 4071 mV full and the
// old 4100 mV gate rejected it; this is the case that failed on the bench.
TEST(FullPoint, ACellThatRestsLowIsStillLearned) {
  FullPointLearner l;
  EXPECT_EQ(4045, learn(l, {{35, 4071, true}, {10, 4045, false}}));
}

// The review's objection: 8 mV is close enough to the noise floor that a flat cell might
// read as still charging and never learn. The learner compares averaged readings, not raw
// samples, so a cell wobbling either side of flat still settles.
TEST(FullPoint, JitterOnAFlatCellStillSettles) {
  FullPointLearner l;
  uint32_t t = 0;
  const int jitter[] = {0, 2, -1, 1, -2, 1, 2, -2};   // +-2 mV, no trend
  for (int m = 0; m < 40; m++) run(l, t, 1, (uint16_t)(4100 + jitter[m % 8]), true);
  EXPECT_TRUE(l.chargerDone());
  EXPECT_EQ(4090, run(l, t, 10, 4090, false));
}

// The review's other objection, and it is real: VBUS flapping restarts the half hour, so
// a badge on a bad cable may never learn. It fails in the safe direction -- a delayed
// learn, never a wrong 100% -- and this is the test that says so.
TEST(FullPoint, FlappingPowerRestartsTheHalfHour) {
  FullPointLearner l;
  uint32_t t = 0;
  run(l, t, 20, 4100, true);
  t += kSec;
  l.feed(t, 4100, false);                            // one second of nothing
  run(l, t, 20, 4100, true);                         // 40 minutes of USB in total
  EXPECT_FALSE(l.chargerDone()) << "the half hour starts over at the plug-in";
  EXPECT_EQ(0, run(l, t, 10, 4090, false));
  // Left alone for a full half hour, it settles as usual.
  run(l, t, 35, 4100, true);
  EXPECT_EQ(4090, run(l, t, 10, 4090, false));
}

// A quick top-up at a charging table never closes a window, so it cannot teach a 100%
// that is too low.
TEST(FullPoint, AShortTopUpTeachesNothing) {
  FullPointLearner l;
  EXPECT_EQ(0, learn(l, {{20, 4100, true}, {10, 4080, false}}));
}

// The charge finishes part way through: the first window is still climbing, the second is
// flat, and the second is the one that counts.
TEST(FullPoint, AChargeThatFinishesPartWayThroughStillLearns) {
  FullPointLearner l;
  uint32_t t = 0;
  for (int m = 0; m < 20; m++) run(l, t, 1, (uint16_t)(4100 + m), true);   // still climbing
  run(l, t, 45, 4160, true);                                              // then flat
  EXPECT_TRUE(l.chargerDone());
  EXPECT_EQ(4126, run(l, t, 10, 4126, false));
}

// A charge that restarted after a flat window has not closed one of its own, so the
// part-window since that close has to agree before anything is learned.
TEST(FullPoint, ARestartedChargeBlocksTheLearn) {
  FullPointLearner l;
  uint32_t t = 0;
  run(l, t, 35, 4100, true);
  ASSERT_TRUE(l.chargerDone());                     // the charger had stopped
  for (int m = 1; m <= 12; m++) run(l, t, 1, (uint16_t)(4100 + m), true);   // and restarted
  EXPECT_FALSE(l.chargerDone());
  t += kSec;
  l.feed(t, 4090, false);                           // unplugged mid-charge
  EXPECT_EQ(FullPointLearner::kNotLearning, l.event());
  EXPECT_EQ(0, run(l, t, 10, 4090, false));
}

// The average carries the USB reading for about a minute after the unplug (a sixteenth of
// the gap per reading, a reading a second). Learning that number would store the
// charger's voltage, so the first kSettleMs is skipped.
TEST(FullPoint, TheFirstMinuteOnBatteryIsNotLearned) {
  FullPointLearner l;
  uint32_t t = 0;
  run(l, t, 35, 4160, true);
  t += kSec;
  l.feed(t, 4160, false);                           // unplug: the average still says 4160
  uint16_t got = 0;
  for (int s = 1; s <= 59; s++) { t += kSec; l.feed(t, 4155, false); }   // inside the settle
  for (int s = 60; s <= 300; s++) {                 // after it, the settled reading
    t += kSec;
    const uint16_t r = l.feed(t, 4126, false);
    if (r) got = r;
  }
  EXPECT_EQ(4126, got) << "the settled reading, not the one the average carried over";
}

// The real unplug trace from the bench: the reading swings 20 mV with the radio. Taking
// the maximum is what makes that harmless -- a timed sample would have caught 4086.
TEST(FullPoint, LoadSwingsAfterUnpluggingDoNotDragItDown) {
  FullPointLearner l;
  uint32_t t = 0;
  run(l, t, 35, 4160, true);
  uint16_t got = 0;
  for (uint16_t mv : {4106, 4086, 4096, 4088, 4101, 4106, 4096, 4102}) {
    const uint16_t r = run(l, t, 1, mv, false);
    if (r) got = r;
  }
  EXPECT_EQ(4106, got) << "the high-water mark, not whatever the radio was doing";
}

TEST(FullPoint, NothingImplausibleIsEverLearned) {
  FullPointLearner a, b;
  EXPECT_EQ(0, learn(a, {{35, 4150, true}, {10, 3500, false}}));   // below the floor
  EXPECT_EQ(0, learn(b, {{35, 4150, true}, {10, 4600, false}}));   // above the ceiling
}

// Running on battery all along, never charged: there is nothing to learn from.
TEST(FullPoint, BatteryOnlyOperationLearnsNothing) {
  FullPointLearner l;
  EXPECT_EQ(0, learn(l, {{60, 4100, false}}));
}

// Plugged back in before the window closes: that unplug taught nothing, and the plug-in
// starts the half hour over rather than carrying the old one forward.
TEST(FullPoint, PluggingBackInAbandonsTheWindow) {
  FullPointLearner l;
  EXPECT_EQ(0, learn(l, {{35, 4183, true}, {2, 4148, false}, {5, 4183, true}}));
  EXPECT_EQ(0, learn(l, {{10, 4150, false}})) << "five minutes back on USB proves nothing";
  // and the next settled charge still works
  EXPECT_EQ(4150, learn(l, {{35, 4183, true}, {10, 4150, false}}));
}

// The review read the within-window maximum as the across-life behaviour and concluded a
// sagging cell could never be followed. It can: every qualifying unplug starts a fresh
// window, so the value moves down as readily as up.
TEST(FullPoint, AnAgeingCellIsFollowedDown) {
  FullPointLearner l;
  EXPECT_EQ(4180, learn(l, {{35, 4200, true}, {10, 4180, false}}));   // new cell
  EXPECT_EQ(4120, learn(l, {{35, 4190, true}, {10, 4120, false}}));   // a year later
  EXPECT_EQ(3980, learn(l, {{35, 4150, true}, {10, 3980, false}}));   // tired
}

// The line between the two measured populations: a half hour that gained kFlatRiseMv is
// the charger done, one millivolt more is the charger still working.
TEST(FullPoint, TheFlatWindowIsWhereItSays) {
  for (int rise = 8; rise <= 9; rise++) {
    FullPointLearner l;
    uint32_t t = 0;
    run(l, t, 30, 4100, true);
    run(l, t, 1, (uint16_t)(4100 + rise), true);     // the half hour now spans the rise
    EXPECT_EQ(rise == 8, l.chargerDone()) << "rise of " << rise << " mV in half an hour";
    EXPECT_EQ(rise == 8 ? 4090 : 0, run(l, t, 10, 4090, false))
        << "rise of " << rise << " mV in half an hour";
  }
}

// Deadlines are compared by difference, so both windows -- the half hour on USB and the
// five minutes after the unplug -- still end across a millis() wrap.
TEST(FullPoint, AWindowSurvivesAMillisWrap) {
  FullPointLearner l;
  uint32_t t = 0xFFFF0000UL;                       // ~4.3 s before the wrap
  run(l, t, 35, 4183, true);                       // the half hour spans the wrap
  ASSERT_TRUE(l.chargerDone()) << "the rolling half hour survived the wrap";
  EXPECT_EQ(4148, run(l, t, 10, 4148, false));
}

// End to end, in the numbers the badge actually sees. The learner is fed the board's own
// ADC, which reads about 22 mV below the rig's gauge at rest -- so the owner's cell, 4183
// charging and 4148 rested on the gauge, arrives here as roughly 4161 and 4126. That
// difference is the whole reason the badge must learn its OWN number rather than be told
// the cell's.
TEST(FullPoint, WhatIsLearnedIsWhatReadsAsFull) {
  FullPointLearner l;
  const uint16_t full = learn(l, {{35, 4161, true}, {10, 4126, false}});
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
// and nowhere else. This walks the owner's 2026-09-17 case in the badge's own numbers: on
// USB at 4073 mV, unplugged too soon, then left alone until the charger settles.
TEST(FullPoint, EachEventFiresAtTheStepItNames) {
  FullPointLearner l;
  uint32_t t = 0;

  t += kSec;
  l.feed(t, 4073, true);
  EXPECT_EQ(FullPointLearner::kPluggedIn, l.event());     // USB seen
  EXPECT_EQ(FullPointLearner::kNone, runEvent(l, t, 5, 4073, true))
      << "five minutes on USB decides nothing";

  t += kSec;
  l.feed(t, 4040, false);
  EXPECT_EQ(FullPointLearner::kNotLearning, l.event());   // unplugged before the half hour
  EXPECT_EQ(4073, l.chargeMv());                          // and the number it stopped on
  EXPECT_EQ(0, run(l, t, 10, 4040, false));
  EXPECT_EQ(FullPointLearner::kNone, l.event());          // not watching, so silent

  // Back on USB, left alone this time.
  t += kSec;
  l.feed(t, 4121, true);
  EXPECT_EQ(FullPointLearner::kPluggedIn, l.event());
  for (int m = 0; m < 29; m++) {
    EXPECT_EQ(FullPointLearner::kNone, runEvent(l, t, 1, 4121, true))
        << "minute " << m << " is inside the half hour";
  }
  EXPECT_EQ(FullPointLearner::kChargerDone, runEvent(l, t, 1, 4121, true));
  EXPECT_EQ(0, l.riseMv());
  EXPECT_TRUE(l.chargerDone());

  t += kSec;
  l.feed(t, 4121, false);
  EXPECT_EQ(FullPointLearner::kWatching, l.event());
  EXPECT_EQ(4121, run(l, t, 5, 4121, false)) << "the window closes five minutes later";
  EXPECT_EQ(FullPointLearner::kLearned, l.event()) << "on the feed that closes it";
}

// The window opens on the unplug and closes on the first feed at or after kWatchMs; that
// closing feed, and only that one, reports. Fed a minute apart, the unplug is minute 0 and
// the close is minute 5.
TEST(FullPoint, AnImplausibleWindowReportsOnTheClosingFeedOnly) {
  FullPointLearner l;
  uint32_t t = 0;
  run(l, t, 35, 4150, true);
  t += kSec;
  l.feed(t, 3500, false);
  EXPECT_EQ(FullPointLearner::kWatching, l.event());
  for (int m = 1; m < 5; m++) {
    EXPECT_EQ(FullPointLearner::kNone, runEvent(l, t, 1, 3500, false))
        << "minute " << m << " is still inside the window";
  }
  EXPECT_EQ(0, run(l, t, 1, 3500, false));
  EXPECT_EQ(FullPointLearner::kImplausible, l.event()) << "the fifth minute closes it";
  EXPECT_EQ(3500, l.bestMv());
  EXPECT_EQ(FullPointLearner::kNone, runEvent(l, t, 1, 3500, false)) << "and it says so once";
}

// What a learn reports is what it measured: bestMv at the closing feed is the value
// returned, so the log line and the stored value cannot disagree.
TEST(FullPoint, ALearnReportsTheValueItReturns) {
  FullPointLearner l;
  uint32_t t = 0;
  run(l, t, 35, 4161, true);
  run(l, t, 1, 4110, false);                        // the unplug minute, inside the settle
  run(l, t, 1, 4126, false);
  run(l, t, 1, 4104, false);
  run(l, t, 1, 4119, false);
  run(l, t, 1, 4101, false);
  t += kSec;                                        // the feed at five minutes closes it
  const uint16_t got = l.feed(t, 4100, false);
  EXPECT_EQ(FullPointLearner::kLearned, l.event());
  EXPECT_EQ(4126, got);
  EXPECT_EQ(got, l.bestMv());
}

TEST(FullPoint, ResetForgetsAPendingWindow) {
  FullPointLearner l;
  uint32_t t = 0;
  run(l, t, 35, 4183, true);
  t += kSec;
  l.feed(t, 4148, false);
  ASSERT_EQ(FullPointLearner::kWatching, l.event());
  l.reset();
  EXPECT_FALSE(l.chargerDone());
  EXPECT_EQ(0, run(l, t, 10, 4148, false));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
