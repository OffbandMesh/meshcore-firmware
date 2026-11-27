// Native tests for the one-session-per-cycle rule (#2232,
// src/helpers/wifi_telemetry/TelCoalesce.h). Pure logic, no Arduino.

#include <gtest/gtest.h>
#include "helpers/wifi_telemetry/TelCoalesce.h"

using namespace offband;

TEST(TelCoalesce, PublishDueSecondsAfterPollHoldsTheLink) {
  // R2-SAW 2026-11-26: the poll finished 2.8 s before the publish began.
  EXPECT_TRUE(telHoldLinkForOther(306219, true, false, 306219 + 2800));
}

TEST(TelCoalesce, OtherDueNowOrOverdueHolds) {
  EXPECT_TRUE(telHoldLinkForOther(5000, true, false, 5000));
  EXPECT_TRUE(telHoldLinkForOther(5000, true, false, 1000));
}

TEST(TelCoalesce, OtherMidCycleHolds) {
  EXPECT_TRUE(telHoldLinkForOther(5000, true, true, 5000 + 300000));
}

TEST(TelCoalesce, OtherFarOffTearsDown) {
  // Just rescheduled a full interval out: this trigger must tear down.
  EXPECT_FALSE(telHoldLinkForOther(5000, true, false, 5000 + 300000));
}

TEST(TelCoalesce, WindowBoundary) {
  EXPECT_TRUE(telHoldLinkForOther(1000, true, false, 1000 + kTelCoalesceWindowMs));
  EXPECT_FALSE(telHoldLinkForOther(1000, true, false, 1000 + kTelCoalesceWindowMs + 1));
}

TEST(TelCoalesce, DisabledOtherNeverHolds) {
  EXPECT_FALSE(telHoldLinkForOther(5000, false, true, 5000));
  EXPECT_FALSE(telHoldLinkForOther(5000, false, false, 4000));
}

TEST(TelCoalesce, WrapSafeAcrossMillisRollover) {
  const uint32_t now = 0xFFFFF000u;
  EXPECT_TRUE(telHoldLinkForOther(now, true, false, now + 10000));    // wraps past 0
  EXPECT_FALSE(telHoldLinkForOther(now, true, false, now + 300000));
  EXPECT_TRUE(telHoldLinkForOther(0x00000100u, true, false, 0xFFFFFF00u));  // overdue across wrap
}

TEST(TelCoalesce, NoDeadlockBothDirections) {
  // After A finishes and B is due, A holds. B then runs and reschedules a full
  // interval out; A was rescheduled a full interval out too, so B tears down.
  const uint32_t t = 100000, interval = 300000;
  EXPECT_TRUE(telHoldLinkForOther(t, true, false, t + 2800));                 // A holds for B
  EXPECT_FALSE(telHoldLinkForOther(t + 3300, true, false, t + interval));     // B tears down
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
