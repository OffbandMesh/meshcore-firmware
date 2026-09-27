// #1275: native tests for the noise-floor log rate limit. Pure decision --
// no Arduino, no radio.

#include <gtest/gtest.h>
#include "helpers/radiolib/NoiseFloorLog.h"

using namespace offband::noisefloor;

TEST(NoiseFloorLog, TheFirstFloorOfABootAlwaysLogs) {
  // It is what tells a reader the receiver converged at all.
  EXPECT_TRUE(shouldLog(-104, 0, 0, 0, /*logged_before=*/false));
  EXPECT_TRUE(shouldLog(-104, -104, 500000, 499000, /*logged_before=*/false));
}

TEST(NoiseFloorLog, SampleToSampleJitterInsideTheWindowIsSuppressed) {
  // The flood this exists to kill: the sampler completes ~2x/s and the floor
  // wanders by a dB or two. 124.6 lines per 5 min across the 61 h capture.
  for (int16_t floor : {-100, -101, -102}) {
    EXPECT_FALSE(shouldLog(floor, -101, 10000, 5000, true))
        << "floor " << floor;
  }
}

TEST(NoiseFloorLog, ARealMoveLogsImmediatelyAndIsNeverHeldForATick) {
  // 3 dB is a doubling of noise power. Measured over the capture, this bound
  // means a >=3 dB change waits 0.0 s -- it is never deferred to the heartbeat,
  // which is what a coarser threshold cost (30.4 s at 6 dB).
  EXPECT_TRUE(shouldLog(-98, -101, 10000, 5000, true));    // floor rose 3
  EXPECT_TRUE(shouldLog(-104, -101, 10000, 5000, true));   // floor fell 3
  EXPECT_TRUE(shouldLog(-95, -101, 10000, 5000, true));    // a big rise
  EXPECT_TRUE(shouldLog(-120, -60, 10000, 5000, true));    // stuck-at-clamp
}

TEST(NoiseFloorLog, TheBoundaryIsInclusiveAndJustUnderItIsNot) {
  EXPECT_TRUE(shouldLog(-101 + kSignificantMoveDb, -101, 10000, 5000, true));
  EXPECT_FALSE(shouldLog(-101 + kSignificantMoveDb - 1, -101, 10000, 5000, true));
  EXPECT_TRUE(shouldLog(-101 - kSignificantMoveDb, -101, 10000, 5000, true));
  EXPECT_FALSE(shouldLog(-101 - kSignificantMoveDb + 1, -101, 10000, 5000, true));
}

TEST(NoiseFloorLog, AHeartbeatStillLandsOnTheTickWhenNothingMoves) {
  // A capture must not go silent on the floor just because it is steady.
  EXPECT_FALSE(shouldLog(-101, -101, 29999, 0, true));
  EXPECT_TRUE(shouldLog(-101, -101, 30000, 0, true));
  EXPECT_TRUE(shouldLog(-101, -101, 90000, 0, true));
}

TEST(NoiseFloorLog, AMillisWrapNeitherFloodsNorGoesSilent) {
  // Unsigned arithmetic: elapsed time is true across the wrap. A signed
  // version would either force a line every pass or suppress them for 49 days.
  const uint32_t before_wrap = 0xFFFFFFFFu - 1000u;
  EXPECT_FALSE(shouldLog(-101, -101, 0xFFFFFFFFu, before_wrap, true));  // 1 s elapsed
  EXPECT_TRUE(shouldLog(-101, -101, 29000u, before_wrap, true));        // 30 s across the wrap
}

TEST(NoiseFloorLog, AStuckFloorStaysVisibleWithoutFloodingTheCapture) {
  // The stuck-at-clamp condition the AGC reset exists for: a constant -120.
  // It must neither vanish from the capture nor flood it. Replays an hour of
  // sample sets at the real ~2/s rate and counts the lines.
  int logged = 0;
  int16_t last_logged = 0;
  uint32_t last_ms = 0;
  bool before = false;
  for (uint32_t ms = 0; ms < 3600u * 1000u; ms += 500u) {
    if (shouldLog(-120, last_logged, ms, last_ms, before)) {
      logged++;
      last_logged = -120;
      last_ms = ms;
      before = true;
    }
  }
  EXPECT_EQ(120, logged) << "one at t=0, then one per 30 s for an hour";
  // The old code would have emitted one per sample set.
  EXPECT_LT(logged, 7200 / 10);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
