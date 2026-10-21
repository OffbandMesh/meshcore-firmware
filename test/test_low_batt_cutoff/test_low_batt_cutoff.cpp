// Native tests for the low-battery cutoff rule (#1667).
//
// This decides whether to power the board off, which is irreversible from the user's
// side and, on a conference badge in someone's hand, indistinguishable from a dead unit.
// The old rule fired on one raw ADC sample and took the owner's badge down with its
// gauge reading ~80%. So the case that matters most here is the one that must NOT fire.

#include <gtest/gtest.h>

#include <cstdint>

#include <helpers/ui/LowBattCutoff.h>

using namespace offband;

namespace {

constexpr uint16_t kThreshold = 3400;   // AUTO_SHUTDOWN_MILLIVOLTS on the badge
constexpr uint32_t kSustain = 300000;   // the owner's 300 s
constexpr uint32_t kTick = 8000;        // the check cadence

// Drive the rule repeatedly and report the time at which it first fires, or 0 for never.
uint32_t fireTime(uint16_t raw, uint16_t smoothed, uint32_t sustain,
                  uint32_t run_ms = 600000, bool external = false) {
  LowBattCutoffState st{};
  for (uint32_t t = 0; t <= run_ms; t += kTick) {
    if (lowBattShouldShutdown(st, t, raw, smoothed, kThreshold, sustain, external)) return t;
  }
  return 0;
}

}  // namespace

// ---- the reported bug ------------------------------------------------------------------

TEST(LowBatt, ARawOutlierAgainstAHealthyAverageNeverFires) {
  // THE BUG, as a test. The owner's badge: gauge and an independent MAX17048 + INA228
  // agreed at ~4026 mV, and a single raw read under 3400 shut it down.
  EXPECT_FALSE(lowBattReadingIsLow(3380, 4026, kThreshold));
  EXPECT_EQ(0u, fireTime(3380, 4026, kSustain))
      << "a raw read below threshold must not fire while the average is healthy";

  // Even an absurd raw read. One sample cannot move an average past the line.
  EXPECT_EQ(0u, fireTime(1, 4026, kSustain));
  EXPECT_EQ(0u, fireTime(2877, 4026, kSustain));
}

TEST(LowBatt, TheReverseIsAlsoRefused) {
  // A stale or badly seeded average must not power the board off either while the live
  // reading says the cell is fine.
  EXPECT_FALSE(lowBattReadingIsLow(4026, 3380, kThreshold));
  EXPECT_EQ(0u, fireTime(4026, 3380, kSustain));
}

// ---- the sustain window ----------------------------------------------------------------

TEST(LowBatt, BothLowFiresOnlyAfterTheFullWindow) {
  const uint32_t at = fireTime(3350, 3360, kSustain);
  ASSERT_NE(0u, at) << "a genuinely low cell must still shut the board down";
  // First low reading is at t=0, so it fires on the first tick at or past the window.
  EXPECT_GE(at, kSustain);
  EXPECT_LT(at, kSustain + kTick) << "it must not overshoot by more than one check";
}

TEST(LowBatt, AShorterRunDoesNotFire) {
  // 296 s of being under the line is not 300 s.
  EXPECT_EQ(0u, fireTime(3350, 3360, kSustain, kSustain - kTick));
}

TEST(LowBatt, OneGoodReadingResetsTheClock) {
  // The point of a debounce: a recovery mid-window starts the count again rather than
  // banking the time already served.
  LowBattCutoffState st{};
  uint32_t t = 0;
  for (; t < kSustain - kTick; t += kTick) {
    ASSERT_FALSE(lowBattShouldShutdown(st, t, 3350, 3360, kThreshold, kSustain, false));
  }
  EXPECT_GT(lowBattHeldMs(st, t), 0u);

  // One healthy pair.
  EXPECT_FALSE(lowBattShouldShutdown(st, t, 3500, 3505, kThreshold, kSustain, false));
  EXPECT_EQ(0u, lowBattHeldMs(st, t));
  t += kTick;

  // Now it needs the whole window again.
  const uint32_t restart = t;
  for (; t < restart + kSustain - kTick; t += kTick) {
    ASSERT_FALSE(lowBattShouldShutdown(st, t, 3350, 3360, kThreshold, kSustain, false))
        << "fired at t=" << t << " after a reset at " << restart;
  }
}

// ---- external power --------------------------------------------------------------------

TEST(LowBatt, ExternalPowerNeverFiresAndClearsHistory) {
  EXPECT_EQ(0u, fireTime(3000, 3000, kSustain, 600000, /*external=*/true));

  // And plugging in mid-window clears rather than pauses: the next unplug starts fresh.
  LowBattCutoffState st{};
  uint32_t t = 0;
  for (; t < kSustain - kTick; t += kTick) {
    lowBattShouldShutdown(st, t, 3350, 3360, kThreshold, kSustain, false);
  }
  EXPECT_FALSE(lowBattShouldShutdown(st, t, 3350, 3360, kThreshold, kSustain, true));
  EXPECT_EQ(0u, lowBattHeldMs(st, t));
  t += kTick;
  EXPECT_FALSE(lowBattShouldShutdown(st, t, 3350, 3360, kThreshold, kSustain, false))
      << "an unplug must not inherit the time served while plugged in";
}

// ---- the disable, which is what a burndown image uses ----------------------------------

TEST(LowBatt, ZeroSustainDisablesTheCutoffEntirely) {
  EXPECT_EQ(0u, fireTime(3000, 3000, 0, 86400000u))
      << "sustain 0 must never fire, even after a day under the line";
  EXPECT_EQ(0u, fireTime(1, 1, 0, 86400000u));
}

TEST(LowBatt, ZeroSustainStillTracksHowLongItHasBeenLow) {
  // So a burndown log still shows the board sitting under the line; it just does not act.
  LowBattCutoffState st{};
  for (uint32_t t = 0; t <= 100000; t += kTick) {
    EXPECT_FALSE(lowBattShouldShutdown(st, t, 3000, 3010, kThreshold, 0, false));
  }
  EXPECT_GE(lowBattHeldMs(st, 100000), 96000u);
}

// ---- readings that are not readings ----------------------------------------------------

TEST(LowBatt, AZeroReadingIsNotALowBattery) {
  // 0 mV means the sensor is not ready or there is no battery rail -- USB-powered boards
  // read that way, and the original code's `milliVolts > 0` guard existed for it.
  EXPECT_FALSE(lowBattReadingIsLow(0, 0, kThreshold));
  EXPECT_FALSE(lowBattReadingIsLow(0, 4026, kThreshold));
  EXPECT_FALSE(lowBattReadingIsLow(3350, 0, kThreshold));
  EXPECT_EQ(0u, fireTime(0, 0, kSustain));
  EXPECT_EQ(0u, fireTime(0, 3350, kSustain));
}

TEST(LowBatt, AZeroThresholdCannotFire) {
  EXPECT_FALSE(lowBattReadingIsLow(100, 100, 0));
  LowBattCutoffState st{};
  EXPECT_FALSE(lowBattShouldShutdown(st, 0, 100, 100, 0, kSustain, false));
}

TEST(LowBatt, ExactlyAtTheThresholdIsNotBelowIt) {
  EXPECT_FALSE(lowBattReadingIsLow(kThreshold, kThreshold, kThreshold));
  EXPECT_TRUE(lowBattReadingIsLow(kThreshold - 1, kThreshold - 1, kThreshold));
}

// ---- the clock -------------------------------------------------------------------------

TEST(LowBatt, TheWindowSurvivesAMillisRollover) {
  // millis() wraps at ~49.7 days. A window straddling the wrap must neither fire early
  // nor be stranded -- unsigned subtraction handles it, and this is the test that says so.
  const uint32_t start = 0xFFFFFFFFu - (kSustain / 2);
  LowBattCutoffState st{};
  ASSERT_FALSE(lowBattShouldShutdown(st, start, 3350, 3360, kThreshold, kSustain, false));

  uint32_t t = start;
  bool fired = false;
  for (uint32_t step = 0; step < kSustain * 2; step += kTick) {
    t = start + step;                       // wraps naturally
    if (lowBattShouldShutdown(st, t, 3350, 3360, kThreshold, kSustain, false)) {
      EXPECT_GE(step, kSustain) << "fired early across the rollover";
      EXPECT_LT(step, kSustain + kTick);
      fired = true;
      break;
    }
  }
  EXPECT_TRUE(fired) << "the window never completed across the rollover";
}

TEST(LowBatt, AnIrregularCadenceStillHonoursTheWindow) {
  // The rule is time-based, so a caller whose loop stalls must not get an early cutoff.
  LowBattCutoffState st{};
  ASSERT_FALSE(lowBattShouldShutdown(st, 0, 3350, 3360, kThreshold, kSustain, false));
  // One enormous gap, well past the window.
  EXPECT_TRUE(lowBattShouldShutdown(st, kSustain + 1, 3350, 3360, kThreshold, kSustain, false));

  // And the converse: many rapid calls inside the window must not accumulate into a fire.
  LowBattCutoffState st2{};
  for (uint32_t t = 0; t < kSustain; t += 10) {
    ASSERT_FALSE(lowBattShouldShutdown(st2, t, 3350, 3360, kThreshold, kSustain, false))
        << "fired at t=" << t << " from call count rather than elapsed time";
  }
}

// ---- the shipped configuration ---------------------------------------------------------

TEST(LowBatt, TheCompiledDefaultIsTheOwnersFigure) {
  // 300 s, and the static_assert in the header refuses anything twitchier than a few
  // check intervals. Pinned so a later edit has to be deliberate.
  EXPECT_EQ(300000u, (uint32_t)OFFBAND_LOW_BATT_SUSTAIN_MS);
  EXPECT_GE((uint32_t)OFFBAND_LOW_BATT_SUSTAIN_MS, 4 * kLowBattCheckIntervalMs);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
