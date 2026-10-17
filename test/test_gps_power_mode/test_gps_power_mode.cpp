// Native tests for GPS Off / On / Power save with backoff (#1554).
//
// This machine decides when a radio that draws 66-77 mA is powered, on a badge that will
// spend a conference weekend indoors where it cannot get a fix at all (#1553). Every rule
// below is a rule about whether that badge survives the weekend, which is why they are
// tested here and not discovered on hardware.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <helpers/GpsPowerMode.h>

using namespace offband;

namespace {

// Runs the machine forward. `fix_after_ms` is how long an attempt takes to succeed once
// GPS is powered; UINT32_MAX never fixes, which is the indoor case.
struct Sim {
  uint32_t ms = 0;
  int attempts = 0, fixes = 0, failures = 0;
  uint32_t powered_ms = 0;          // total time GPS was on
  std::vector<uint32_t> waits;      // each wait as it began

  void go(GpsPowerMachine& m, uint32_t fix_after_ms, uint32_t limit_ms,
          uint32_t step_ms = 1000) {
    uint32_t attempt_began = 0;
    bool was_on = false;
    bool fix = false;
    while (ms < limit_ms) {
      const GpsPowerAction a = m.tick(ms, fix);
      if (a.attempt_started) { attempts++; attempt_began = ms; }
      if (a.fix_acquired) fixes++;
      if (a.attempt_failed) failures++;
      if (a.next_wait_ms) {
        if (waits.empty() || waits.back() != a.next_wait_ms || a.fix_acquired ||
            a.attempt_failed) {
          waits.push_back(a.next_wait_ms);
        }
      }
      if (a.gps_on) powered_ms += step_ms;
      if (a.gps_on && !was_on) attempt_began = ms;
      was_on = a.gps_on;
      fix = a.gps_on && fix_after_ms != UINT32_MAX && (ms - attempt_began) >= fix_after_ms;
      ms += step_ms;
    }
  }
};

constexpr uint32_t kHour = 3600000u;

}  // namespace

// ---- Off and On are unconditional ------------------------------------------------------

TEST(GpsPowerMode, OffNeverPowersTheRadio) {
  GpsPowerMachine m;
  m.setMode(GpsMode::Off, 0);
  for (uint32_t t = 0; t < 12 * kHour; t += 60000) {
    const GpsPowerAction a = m.tick(t, false);
    ASSERT_FALSE(a.gps_on) << "powered at t=" << t;
    ASSERT_EQ(GpsActivity::Off, a.activity);
  }
}

// On is today's behaviour and must be bit-for-bit unchanged: powered, always, and a fix
// arriving or not changes nothing.
TEST(GpsPowerMode, OnStaysPoweredWhateverHappens) {
  GpsPowerMachine m;
  m.setMode(GpsMode::On, 0);
  for (uint32_t t = 0; t < 12 * kHour; t += 60000) {
    const GpsPowerAction a = m.tick(t, t % 2 == 0);
    ASSERT_TRUE(a.gps_on) << "unpowered at t=" << t;
    ASSERT_EQ(GpsActivity::On, a.activity);
  }
}

// ---- Power save, with sky --------------------------------------------------------------

// A badge that can fix settles into the plain interval and never backs off.
TEST(GpsPowerMode, WithSkyItSettlesIntoTheIntervalAndNeverBacksOff) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  Sim r;
  r.go(m, 20000, 12 * kHour);

  EXPECT_EQ(0, r.failures) << "a badge that fixes must never back off";
  EXPECT_EQ(0, m.failures());
  // 12 hours at an hourly interval: the boot attempt plus roughly one an hour.
  EXPECT_GE(r.fixes, 11);
  EXPECT_LE(r.fixes, 13);
  for (uint32_t w : r.waits) {
    EXPECT_EQ(kGpsIntervalMs, w) << "the wait grew even though nothing failed";
  }
}

// The point of the whole feature, stated as a number. Idle is 13.7 mA and searching is
// ~70 mA (#1553), so what matters is the fraction of time powered.
TEST(GpsPowerMode, WithSkyTheRadioIsPoweredUnderOnePercentOfTheTime) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  Sim r;
  r.go(m, 20000, 24 * kHour);

  const double duty = (double)r.powered_ms / (24.0 * kHour);
  EXPECT_LT(duty, 0.01) << "powered " << duty * 100 << "% of the day";
}

// ---- Power save, indoors: the conference case -----------------------------------------

// Nothing ever fixes. The wait has to grow and then stop growing, and the badge has to
// stop grinding. Without backoff this is ~48 attempts a weekend at ~1.4 mAh each.
TEST(GpsPowerMode, IndoorsTheWaitDoublesToTheCapAndStaysThere) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  Sim r;
  r.go(m, UINT32_MAX, 72 * kHour);

  ASSERT_GE(r.waits.size(), (size_t)6);
  // Nothing ever succeeds here, so the first wait recorded is the one after failure #1 --
  // and it is the plain interval, because one missed fix is not a pattern and a transient
  // miss should cost no more than a success does. Doubling runs from failure #2:
  // 1x, 2x, 4x, 8x, then pinned at the cap.
  EXPECT_EQ(1 * kHour, r.waits[0]) << "the first failure was treated as a pattern";
  EXPECT_EQ(2 * kHour, r.waits[1]);
  EXPECT_EQ(4 * kHour, r.waits[2]);
  EXPECT_EQ(8 * kHour, r.waits[3]);
  for (size_t i = 4; i < r.waits.size(); i++) {
    EXPECT_EQ(8 * kHour, r.waits[i]) << "wait " << i << " is not pinned at the cap";
  }
}

// The number that decides whether a con badge lasts: how many futile attempts over a
// 48-hour weekend. Hourly retries would be ~48.
TEST(GpsPowerMode, IndoorsAWeekendCostsUnderADozenAttempts) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  Sim r;
  r.go(m, UINT32_MAX, 48 * kHour);

  EXPECT_LE(r.attempts, 12) << "backoff is not reducing the attempt count";
  EXPECT_EQ(r.attempts, r.failures) << "an attempt neither fixed nor failed";
  // And the radio is barely on: each attempt is one 90 s timeout.
  const double duty = (double)r.powered_ms / (48.0 * kHour);
  EXPECT_LT(duty, 0.01) << "powered " << duty * 100 << "% of the weekend";
}

// Walking outside has to bring it back immediately -- no reboot, no trip to Settings.
// This is the rule that makes backoff acceptable rather than a trap.
TEST(GpsPowerMode, OneSuccessResetsTheBackoffCompletely) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  Sim indoors;
  indoors.go(m, UINT32_MAX, 30 * kHour);
  ASSERT_GT(m.failures(), 0u) << "it never backed off, so there is nothing to reset";

  // Now it can fix. The next attempt succeeds and the wait must be back to the interval.
  Sim outside;
  outside.ms = indoors.ms;
  outside.go(m, 20000, indoors.ms + 12 * kHour);

  EXPECT_EQ(0, (int)m.failures()) << "a success did not clear the failure count";
  ASSERT_FALSE(outside.waits.empty());
  EXPECT_EQ(kGpsIntervalMs, outside.waits.back())
      << "still waiting a backed-off interval after a fix";
}

// ---- what the screen is told (#1557) ---------------------------------------------------

// Sleeping and BackedOff are different states and must not be conflated: one is normal,
// the other is "I have given up for now", which is what a con attendee needs to see.
TEST(GpsPowerMode, SleepingAndBackedOffAreDistinguishable) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);

  // A success, then the wait: Sleeping.
  uint32_t t = 0;
  bool seen_sleeping = false, seen_backed_off = false, seen_searching = false;
  for (; t < 2 * kHour; t += 1000) {
    const GpsPowerAction a = m.tick(t, t < 30000);   // fixes almost at once, then not
    if (a.activity == GpsActivity::Sleeping) seen_sleeping = true;
    if (a.activity == GpsActivity::Searching) seen_searching = true;
  }
  EXPECT_TRUE(seen_searching);
  EXPECT_TRUE(seen_sleeping);

  // Now starve it and watch for BackedOff.
  for (; t < 30 * kHour; t += 1000) {
    const GpsPowerAction a = m.tick(t, false);
    if (a.activity == GpsActivity::BackedOff) seen_backed_off = true;
  }
  EXPECT_TRUE(seen_backed_off) << "the screen can never say it has given up";
}

// Searching is reported only while the radio is actually on. A screen that says
// "searching" with the radio down is lying about where the battery is going.
TEST(GpsPowerMode, SearchingIsReportedOnlyWhilePowered) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  for (uint32_t t = 0; t < 30 * kHour; t += 1000) {
    const GpsPowerAction a = m.tick(t, false);
    if (a.activity == GpsActivity::Searching) ASSERT_TRUE(a.gps_on) << "at t=" << t;
    if (a.activity == GpsActivity::Sleeping || a.activity == GpsActivity::BackedOff) {
      ASSERT_FALSE(a.gps_on) << "at t=" << t;
    }
  }
}

// ---- boot, mode changes, and the stored byte -------------------------------------------

// The clock is what GPS is for on this badge, so Power save attempts at once rather than
// sitting dark for the first hour.
TEST(GpsPowerMode, PowerSaveAttemptsImmediatelyOnEntry) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  const GpsPowerAction a = m.tick(0, false);
  EXPECT_TRUE(a.gps_on);
  EXPECT_EQ(GpsActivity::Searching, a.activity);
}

TEST(GpsPowerMode, AModeChangeMidAttemptTakesEffectAtOnce) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  ASSERT_TRUE(m.tick(1000, false).gps_on);

  m.setMode(GpsMode::Off, 2000);
  EXPECT_FALSE(m.tick(2000, false).gps_on) << "Off did not cut power immediately";

  m.setMode(GpsMode::On, 3000);
  EXPECT_TRUE(m.tick(3000, false).gps_on) << "On did not power up immediately";

  // And returning to Power save starts a fresh attempt with no inherited failures.
  m.setMode(GpsMode::PowerSave, 4000);
  const GpsPowerAction a = m.tick(4000, false);
  EXPECT_TRUE(a.gps_on);
  EXPECT_EQ(0, (int)m.failures()) << "failures carried across a mode change";
}

// An unrecognised byte is a pref from a build that knew more modes. It falls back to On --
// what this firmware did before Power save existed. Never to Power save, which would be a
// badge that mysteriously stops holding a fix; never to Off, because silence is a choice
// someone makes, the same argument #1522 made for tune ids.
TEST(GpsPowerMode, AnUnknownStoredModeFallsBackToOn) {
  EXPECT_EQ(GpsMode::Off, gpsModeFromStored(0));
  EXPECT_EQ(GpsMode::On, gpsModeFromStored(1));
  EXPECT_EQ(GpsMode::PowerSave, gpsModeFromStored(2));
  for (int b : {3, 4, 17, 99, 254, 255}) {
    EXPECT_EQ(GpsMode::On, gpsModeFromStored((uint8_t)b)) << "stored " << b;
    EXPECT_NE(GpsMode::PowerSave, gpsModeFromStored((uint8_t)b)) << "stored " << b;
    EXPECT_FALSE(isValidGpsMode((uint8_t)b)) << "stored " << b;
  }
}

// ---- the two attempt timeouts (review's critical finding) ------------------------------

// The trap this exists for: a 90 s timeout tuned on #1553's SHORT power-offs (16 s after
// 30 s off, 69 s after 5 min) would fail a genuine cold start after the hour-long sleep
// the machine actually uses -- and back the badge off WHILE IT IS OUTSIDE, which breaks
// the one promise backoff rests on.
TEST(GpsAttemptTimeout, AnOutdoorColdStartSlowerThanTheProbeTimeoutStillSucceeds) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  Sim r;
  // 150 s to fix: slower than the 90 s probe timeout, well inside the 180 s first attempt.
  r.go(m, 150000, 12 * kHour);

  EXPECT_GT(r.fixes, 0) << "an outdoor cold start was timed out as if it were indoors";
  EXPECT_EQ(0, r.failures) << "a badge with sky was pushed into backoff";
  EXPECT_EQ(0, (int)m.failures());
}

TEST(GpsAttemptTimeout, TheFirstAttemptIsGenerousAndLaterProbesAreCheap) {
  GpsPowerConfig cfg;
  EXPECT_EQ(kGpsFirstAttemptTimeoutMs, gpsAttemptTimeoutMs(cfg, 0));
  EXPECT_GT(gpsAttemptTimeoutMs(cfg, 0), gpsAttemptTimeoutMs(cfg, 1))
      << "a probe must cost less than a serious attempt";
  for (int f : {1, 2, 5, 255}) {
    EXPECT_EQ(kGpsProbeAttemptTimeoutMs, gpsAttemptTimeoutMs(cfg, (uint8_t)f));
  }
}

// And a success has to restore the generous timeout, or a badge that recovers once is
// permanently stuck with probe-length attempts.
TEST(GpsAttemptTimeout, ASuccessRestoresTheGenerousTimeout) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  Sim indoors;
  indoors.go(m, UINT32_MAX, 30 * kHour);
  ASSERT_GT(m.failures(), 0u);
  EXPECT_EQ(kGpsProbeAttemptTimeoutMs, gpsAttemptTimeoutMs(GpsPowerConfig(), m.failures()));

  // Outside, where acquisition takes 150 s -- longer than a probe allows. It has to get
  // there via one probe-length attempt that fails, then... it never would. So the rule
  // that matters is: once ANY fix lands, the generous timeout is back.
  Sim outside;
  outside.ms = indoors.ms;
  outside.go(m, 60000, indoors.ms + 12 * kHour);   // 60 s: inside even a probe
  ASSERT_EQ(0, (int)m.failures()) << "no fix landed, so the premise of this test is gone";
  EXPECT_EQ(kGpsFirstAttemptTimeoutMs, gpsAttemptTimeoutMs(GpsPowerConfig(), m.failures()));
}

// ---- forcing a retry out of backoff ----------------------------------------------------

// The user-facing escape hatch, and the owner asked for it by name: someone staring at
// "backed off" with hours to wait needs a way to say "try now".
//
// setMode() IS that gesture -- it clears the failure count, backdates the wait so the next
// tick attempts immediately, and restores the generous timeout. Pinned here so a later
// change cannot quietly make a mode re-selection cheap-and-stateless and take the escape
// hatch away with it. The screen's job is not to swallow a selection that does not change
// the value (#1557).
TEST(GpsForceRetry, ReselectingPowerSaveWhileBackedOffRetriesAtOnce) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  Sim indoors;
  indoors.go(m, UINT32_MAX, 30 * kHour);
  const uint8_t backed_off = m.failures();
  ASSERT_GT(backed_off, 1u) << "it never reached a long wait, so there is nothing to escape";

  // The gesture: pick Power save again.
  const uint32_t t = indoors.ms;
  m.setMode(GpsMode::PowerSave, t);

  EXPECT_EQ(0, (int)m.failures()) << "the failure count survived a re-selection";
  const GpsPowerAction a = m.tick(t, false);
  EXPECT_TRUE(a.gps_on) << "no immediate attempt -- the user is still waiting";
  EXPECT_TRUE(a.attempt_started);
  EXPECT_EQ(GpsActivity::Searching, a.activity);
  EXPECT_EQ(kGpsFirstAttemptTimeoutMs, gpsAttemptTimeoutMs(GpsPowerConfig(), m.failures()))
      << "the forced retry got a probe-length attempt instead of a real try";
}

// Going out through Off and back is the other route a user might take, and it has to work
// the same way rather than leaving the old failure count in place.
TEST(GpsForceRetry, ATripThroughOffAlsoClearsTheBackoff) {
  GpsPowerMachine m;
  m.setMode(GpsMode::PowerSave, 0);
  Sim indoors;
  indoors.go(m, UINT32_MAX, 30 * kHour);
  ASSERT_GT(m.failures(), 1u);

  uint32_t t = indoors.ms;
  m.setMode(GpsMode::Off, t);
  EXPECT_FALSE(m.tick(t, false).gps_on);
  t += 5000;
  m.setMode(GpsMode::PowerSave, t);
  EXPECT_EQ(0, (int)m.failures());
  EXPECT_TRUE(m.tick(t, false).attempt_started);
}

// ---- the backoff arithmetic itself -----------------------------------------------------

TEST(GpsBackoff, TheWaitDoublesFromTheSecondFailureAndSaturatesAtTheCap) {
  GpsPowerConfig cfg;
  // A success and a single failure both wait the plain interval. Backoff is for
  // CONSECUTIVE failure; one miss is not a pattern.
  EXPECT_EQ(1 * kHour, gpsBackoffWaitMs(cfg, 0));
  EXPECT_EQ(1 * kHour, gpsBackoffWaitMs(cfg, 1));
  EXPECT_EQ(2 * kHour, gpsBackoffWaitMs(cfg, 2));
  EXPECT_EQ(4 * kHour, gpsBackoffWaitMs(cfg, 3));
  EXPECT_EQ(8 * kHour, gpsBackoffWaitMs(cfg, 4));
  for (int f : {5, 10, 200, 255}) {
    EXPECT_EQ(8 * kHour, gpsBackoffWaitMs(cfg, (uint8_t)f)) << "failures=" << f;
  }
}

// The cap is a doubling count, so it has to bound the wait for ANY failure count -- the
// loop that applies it is the only thing standing between a long outage and an absurd
// wait.
TEST(GpsBackoff, TheCapBoundsTheWaitForEveryPossibleFailureCount) {
  GpsPowerConfig cfg;
  const uint32_t ceiling = cfg.interval_ms << cfg.max_backoff_shift;
  for (int f = 0; f <= 255; f++) {
    const uint32_t w = gpsBackoffWaitMs(cfg, (uint8_t)f);
    EXPECT_GE(w, cfg.interval_ms) << "failures=" << f << " waited less than the interval";
    EXPECT_LE(w, ceiling) << "failures=" << f << " exceeded the cap";
  }
}

// A wait that overflowed would come back SHORT, turning backoff into a tight retry loop --
// the exact failure it exists to prevent, and silent.
TEST(GpsBackoff, AnAbsurdIntervalSaturatesRatherThanWrappingToAShortWait) {
  GpsPowerConfig cfg;
  cfg.interval_ms = 0xF0000000u;
  cfg.max_backoff_shift = 8;
  for (uint8_t f = 0; f <= 8; f++) {
    EXPECT_GE(gpsBackoffWaitMs(cfg, f), cfg.interval_ms)
        << "failures=" << (int)f << " produced a SHORTER wait than the interval";
  }
}

// ---- millis() rollover -----------------------------------------------------------------

// The badge runs for days; millis() wraps at ~49.7. A wait computed across the wrap must
// still end, or GPS stays dark until a reboot.
TEST(GpsPowerMode, AWaitSpanningTheMillisRolloverStillEnds) {
  GpsPowerMachine m;
  const uint32_t near_wrap = 0xFFFFFFFFu - (uint32_t)(30 * 60000);   // 30 min short
  m.setMode(GpsMode::PowerSave, near_wrap);

  // Fail the first attempt so a wait begins just before the wrap.
  uint32_t t = near_wrap;
  for (int i = 0; i < 200; i++) { m.tick(t, false); t += 1000; }

  bool attempted_after_wrap = false;
  // Step across the wrap and well past one interval.
  for (int i = 0; i < 8000; i++) {
    const GpsPowerAction a = m.tick(t, false);
    if (a.attempt_started && t < near_wrap) attempted_after_wrap = true;  // t has wrapped
    t += 1000;
  }
  EXPECT_TRUE(attempted_after_wrap)
      << "no attempt after the rollover -- GPS would stay dark until a reboot";
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
