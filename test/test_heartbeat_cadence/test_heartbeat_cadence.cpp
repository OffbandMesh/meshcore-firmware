// Native tests for the #275 heartbeat cadence, and specifically for the rounding trap.
//
// The arithmetic looks trivial and is not: every interval is rounded UP to the loop's wake
// boundary, so the number you write is not the number you get. A value that sits exactly
// ON a boundary resolves differently depending on jitter. These tests pin both the shipped
// cadences and the trap, because a session proposed 4900 for the badge -- exactly on a
// boundary -- and 4890 is what was wanted.

#include <gtest/gtest.h>

#include <helpers/ui/HeartbeatCadence.h>

using namespace offband;

namespace {
// The two cadences that actually ship.
constexpr uint32_t kOn = 90;
constexpr uint32_t kCarOff = 890;     // solar car companion: ~1 Hz
constexpr uint32_t kBadgeOff = 4890;  // QCC badge: ~5 s
}  // namespace

// ---- rounding, which is where the surprises live ---------------------------------------

TEST(Observed, AValueUnderABoundaryRoundsUpToIt) {
  EXPECT_EQ(100u, heartbeatObservedMs(90));
  EXPECT_EQ(900u, heartbeatObservedMs(890));
  EXPECT_EQ(4900u, heartbeatObservedMs(4890));
}

TEST(Observed, AValueExactlyOnABoundaryLandsOnItself) {
  // The negative half of the trap: 4900 does NOT become 4900-then-safe, it becomes 4900
  // only if the comparison is not beaten by jitter -- and the arithmetic cannot express
  // that uncertainty. What it CAN show is that 4890 and 4900 are different requests that
  // produce different observed intervals.
  EXPECT_EQ(4900u, heartbeatObservedMs(4900));
  EXPECT_EQ(100u, heartbeatObservedMs(100));
  EXPECT_NE(heartbeatObservedMs(4900), heartbeatObservedMs(4901));
}

TEST(Observed, JustOverABoundaryCostsAWholeWake) {
  // Why "a hair under" and not "a hair over": 901 costs an entire extra wake period.
  EXPECT_EQ(1000u, heartbeatObservedMs(901));
  EXPECT_EQ(5000u, heartbeatObservedMs(4901));
}

TEST(Observed, ZeroAndDegenerateInputs) {
  EXPECT_EQ(0u, heartbeatObservedMs(0));
  EXPECT_EQ(123u, heartbeatObservedMs(123, 0)) << "a zero wake means no rounding at all";
}

// ---- the two shipped cadences ----------------------------------------------------------

TEST(Cadence, TheCarCompanionDefaultIsOneHertz) {
  EXPECT_EQ(1000u, heartbeatPeriodMs(kOn, kCarOff));
  EXPECT_EQ(100u, heartbeatObservedMs(kOn)) << "lit for one wake period";
  EXPECT_EQ(900u, heartbeatObservedMs(kCarOff));
}

TEST(Cadence, TheBadgeOverrideIsFiveSeconds) {
  EXPECT_EQ(5000u, heartbeatPeriodMs(kOn, kBadgeOff));
  EXPECT_EQ(4900u, heartbeatObservedMs(kBadgeOff)) << "4.9 s dark";
}

TEST(Cadence, TheBadgeIsExactlyFiveTimesSlowerThanTheDefault) {
  // Stated as a relationship rather than two magic numbers, so a change to one that
  // forgets the other shows up here.
  EXPECT_EQ(5 * heartbeatPeriodMs(kOn, kCarOff), heartbeatPeriodMs(kOn, kBadgeOff));
}

TEST(Cadence, TheLitTimeIsUNCHANGEDBetweenThem) {
  // The flash itself cannot be shortened below one wake period without a faster timer,
  // which would cost more power than the LED saves. Only the gap moved.
  EXPECT_EQ(heartbeatObservedMs(kOn), heartbeatObservedMs(kOn));
  EXPECT_EQ(100u, heartbeatObservedMs(kOn));
}

TEST(Cadence, SlowingTheGapCutsTheDuty) {
  // The point of the override, as a property rather than a number: a longer gap with the
  // same lit time is strictly less on-time per second.
  const double car = 100.0 / heartbeatPeriodMs(kOn, kCarOff);
  const double badge = 100.0 / heartbeatPeriodMs(kOn, kBadgeOff);
  EXPECT_GT(car, badge);
  EXPECT_NEAR(0.10, car, 0.001);
  EXPECT_NEAR(0.02, badge, 0.001);
}

// ---- the boundary guard -----------------------------------------------------------------

TEST(BoundaryGuard, TheShippedValuesAreClearOfABoundary) {
  EXPECT_TRUE(heartbeatValueIsClearOfBoundary(kOn));
  EXPECT_TRUE(heartbeatValueIsClearOfBoundary(kCarOff));
  EXPECT_TRUE(heartbeatValueIsClearOfBoundary(kBadgeOff));
}

TEST(BoundaryGuard, AValueOnABoundaryIsFlagged) {
  // THE MISTAKE THIS TEST EXISTS FOR. 4900 was proposed for the badge; it sits exactly on
  // a 100 ms wake boundary, so the observed gap depends on which side of the comparison
  // the jitter falls. 4890 does not.
  EXPECT_FALSE(heartbeatValueIsClearOfBoundary(4900))
      << "4900 is exactly on a wake boundary -- 4890 is the value that wants to be here";
  EXPECT_FALSE(heartbeatValueIsClearOfBoundary(100));
  EXPECT_FALSE(heartbeatValueIsClearOfBoundary(1000));
  EXPECT_FALSE(heartbeatValueIsClearOfBoundary(0));
}

TEST(BoundaryGuard, ADegenerateWakeIsNotClear) {
  EXPECT_FALSE(heartbeatValueIsClearOfBoundary(4890, 0));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
