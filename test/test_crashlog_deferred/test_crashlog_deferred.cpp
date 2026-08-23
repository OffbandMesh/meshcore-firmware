// Native unit tests for the deferred previous-boot crash-dump predicate (#952).
//
// WHAT THIS EXISTS TO CATCH. crashLogTick used to decide with
//
//     if (s_prev_pending && now_ms >= 5000u) { s_prev_pending = false; ... }
//
// clearing the flag BEFORE attempting the write. emitPreviousBootDump then
// returns early when nothing is listening, so on a board with no host attached
// at exactly T+5 s the single opportunity was spent writing into a closed
// transport. On a native-USB board -- which re-enumerates after every reset --
// that is a race no human wins, and #889 lost a real reset reason to it.
//
// Pure logic, no Arduino: CrashLog.h includes only stddef/stdint and selects its
// host path when neither ESP32 nor nRF52 is defined.

#include <gtest/gtest.h>
#include "helpers/diagnostics/CrashLog.h"

using offband::crashLogShouldEmitDeferred;
using offband::kCrashLogDeferMs;

// The pre-fix decision, reproduced verbatim. Kept so the regression this test
// guards is stated in the test rather than only in a commit message.
static bool preFixPredicate(bool pending, uint32_t now_ms, bool /*host_attached*/) {
  return pending && now_ms >= 5000u;
}

// ---------------------------------------------------------------------------
// The defect itself
// ---------------------------------------------------------------------------

TEST(CrashLogDeferred, PreFixLogicSpentTheDumpWithNoHostAttached) {
  // Old logic fired -- and because the caller cleared the flag first, the dump
  // was gone forever.
  EXPECT_TRUE(preFixPredicate(true, kCrashLogDeferMs, false));
  // New logic waits for someone to actually be listening.
  EXPECT_FALSE(crashLogShouldEmitDeferred(true, kCrashLogDeferMs, false));
}

TEST(CrashLogDeferred, NoHostMeansNotEligibleHoweverLongItHasBeen) {
  EXPECT_FALSE(crashLogShouldEmitDeferred(true, kCrashLogDeferMs, false));
  EXPECT_FALSE(crashLogShouldEmitDeferred(true, 60000u, false));
  EXPECT_FALSE(crashLogShouldEmitDeferred(true, 86400000u, false));
}

// The whole point of #378: a monitor plugged in well after the reset.
TEST(CrashLogDeferred, HostAttachingLateStillGetsTheDump) {
  EXPECT_TRUE(crashLogShouldEmitDeferred(true, 60000u, true));
  EXPECT_TRUE(crashLogShouldEmitDeferred(true, 86400000u, true));
}

// ---------------------------------------------------------------------------
// The settle delay, preserved
// ---------------------------------------------------------------------------

TEST(CrashLogDeferred, NotEligibleBeforeTheDeferDelay) {
  EXPECT_FALSE(crashLogShouldEmitDeferred(true, 0u, true));
  EXPECT_FALSE(crashLogShouldEmitDeferred(true, kCrashLogDeferMs - 1u, true));
}

TEST(CrashLogDeferred, EligibleExactlyAtTheDeferDelay) {
  EXPECT_TRUE(crashLogShouldEmitDeferred(true, kCrashLogDeferMs, true));
}

// ---------------------------------------------------------------------------
// Nothing to report
// ---------------------------------------------------------------------------

TEST(CrashLogDeferred, NothingPendingIsNeverEligible) {
  EXPECT_FALSE(crashLogShouldEmitDeferred(false, kCrashLogDeferMs, true));
  EXPECT_FALSE(crashLogShouldEmitDeferred(false, 86400000u, true));
  EXPECT_FALSE(crashLogShouldEmitDeferred(false, 0u, false));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
