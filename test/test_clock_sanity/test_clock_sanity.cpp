// Native unit tests for ClockSanity (#607) — the plausibility window that
// bounds AUTOMATED clock sources (GPS decode, contacts-store RTC bootstrap)
// and heals poisoned persisted lastmod values. Owner paths are deliberately
// ungated and therefore not represented here beyond the audit-log no-crash
// check. Pure logic, no Arduino.

#include <gtest/gtest.h>
#include "helpers/ClockSanity.h"

using namespace offband;

// Fixed reference: floor parsed from a known build date.
// 2026-08-07 00:00:00 UTC = 1786060800 (20672 days × 86400).
static const uint32_t kFloor = 1786060800u;
static const uint32_t kYear = 31557600u;

class ClockSanityTest : public ::testing::Test {
 protected:
  void SetUp() override { _setBuildDateForTest("2026-08-07"); }
};

TEST_F(ClockSanityTest, FloorParsesBuildDate) {
  EXPECT_EQ(kFloor, buildEpochFloor());
}

TEST_F(ClockSanityTest, CeilingIsTwentyYearsOut) {
  EXPECT_EQ(kFloor + 20u * kYear, plausibilityCeiling());
}

TEST_F(ClockSanityTest, WindowEdges) {
  EXPECT_TRUE(plausibleEpoch(kFloor));                    // floor inclusive
  EXPECT_TRUE(plausibleEpoch(plausibilityCeiling()));     // ceiling inclusive
  EXPECT_FALSE(plausibleEpoch(kFloor - 1));               // pre-build: no
  EXPECT_FALSE(plausibleEpoch(plausibilityCeiling() + 1));
  EXPECT_FALSE(plausibleEpoch(0));
}

TEST_F(ClockSanityTest, GpsRolloverClassRejected) {
  // One GPS week-number rollover ≈ +19.6y — just inside a 20y horizon when
  // it happens near build time, so specifically test the OBSERVED failure:
  // the car node sat ~3 rollovers (+59y) out. Decades-future must fail.
  EXPECT_FALSE(plausibleEpoch(kFloor + 59u * kYear));     // the #607 case
  EXPECT_FALSE(plausibleEpoch(kFloor + 21u * kYear));
  EXPECT_TRUE(plausibleEpoch(kFloor + 5u * kYear));       // sane future ok
}

TEST_F(ClockSanityTest, ClampHealsPoisonedLastmod) {
  EXPECT_EQ(kFloor, clampLastmod(kFloor + 59u * kYear));  // future poison -> floor
  EXPECT_EQ(kFloor, clampLastmod(12345u));                // ancient garbage -> floor
  uint32_t sane = kFloor + kYear;
  EXPECT_EQ(sane, clampLastmod(sane));                    // sane passes through
}

TEST_F(ClockSanityTest, BadBuildDateFallsBackConservatively) {
  _setBuildDateForTest("garbage");
  // Fallback floor = 2026-01-01 (1767225600), never zero.
  EXPECT_EQ(1767225600u, buildEpochFloor());
  _setBuildDateForTest("2026-08-07");
}

TEST_F(ClockSanityTest, LogClockSetIsNoCrashOffTarget) {
  logClockSet("test", kFloor, kFloor + 10);  // host build: no-op, must not crash
  SUCCEED();
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
