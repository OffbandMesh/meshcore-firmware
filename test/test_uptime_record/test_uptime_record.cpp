// #1074: native tests for the previous-boot uptime logic -- the NVS save
// schedule, the RTC-retained record, and which one the boot line reports.

#include <gtest/gtest.h>
#include <vector>
#include "helpers/diagnostics/UptimeRecord.h"

using namespace offband::uptime;

namespace {

// Run the NVS schedule the way the main loop does (a tick every 10 ms) from
// `from` for `ms`, returning the times a save happened.
std::vector<uint32_t> runNvs(uint32_t from, uint32_t ms) {
  std::vector<uint32_t> saves;
  bool saved = false;
  uint32_t last = 0;
  for (uint32_t t = from; t - from < ms; t += 10) {
    if (nvsSaveDue(t, last, saved)) {
      saves.push_back(t);
      last = t;
      saved = true;
    }
  }
  return saves;
}

}  // namespace

TEST(UptimeNvsSchedule, FirstSaveWaitsForTheFastInterval) {
  EXPECT_FALSE(nvsSaveDue(0, 0, false));
  EXPECT_FALSE(nvsSaveDue(kNvsFastMs - 1, 0, false));
  EXPECT_TRUE(nvsSaveDue(kNvsFastMs, 0, false));
}

TEST(UptimeNvsSchedule, FastThenSlowThenStops) {
  const std::vector<uint32_t> saves = runNvs(0, 60u * 60u * 1000u);   // one hour
  ASSERT_FALSE(saves.empty());
  // Fast phase: every 5 s up to 2 min.
  EXPECT_EQ(5000u, saves[0]);
  EXPECT_EQ(10000u, saves[1]);
  // Nothing saved after the first save at or past 15 min.
  const uint32_t last = saves.back();
  EXPECT_GE(last, kNvsStopAfterMs);
  EXPECT_LT(last, kNvsStopAfterMs + kNvsSlowMs);
  // Slow phase spacing is 60 s.
  EXPECT_EQ(kNvsSlowMs, saves[saves.size() - 1] - saves[saves.size() - 2]);
}

TEST(UptimeNvsSchedule, SwitchesToTheSlowIntervalAtTwoMinutes) {
  // Last fast save at 115 s: the next is due 60 s later, not 5 s later.
  EXPECT_FALSE(nvsSaveDue(kNvsFastUntilMs, 115000, true));
  EXPECT_FALSE(nvsSaveDue(174999, 115000, true));
  EXPECT_TRUE(nvsSaveDue(175000, 115000, true));
  // Still in the fast phase just before 2 min.
  EXPECT_TRUE(nvsSaveDue(kNvsFastUntilMs - 1, kNvsFastUntilMs - 1 - kNvsFastMs, true));
}

TEST(UptimeNvsSchedule, WritesPerBootAreBoundedRegardlessOfUptime) {
  const size_t one_hour = runNvs(0, 60u * 60u * 1000u).size();
  const size_t three_hours = runNvs(0, 3u * 60u * 60u * 1000u).size();
  EXPECT_EQ(one_hour, three_hours);   // stopped: uptime no longer costs writes
  EXPECT_LE(one_hour, 40u);
  EXPECT_GE(one_hour, 30u);
}

TEST(UptimeNvsSchedule, MillisWrapDoesNotRestartAStoppedSchedule) {
  // Stopped at ~15 min; millis() later wraps to a small value.
  EXPECT_FALSE(nvsSaveDue(1000, kNvsStopAfterMs + 30000, true));
  EXPECT_FALSE(nvsSaveDue(0xFFFFFFF0u, kNvsStopAfterMs + 30000, true));
}

TEST(UptimeRtcRecord, StampedRecordValidatesAndGarbageDoesNot) {
  RetainedUptime r{};
  stamp(r, 41, 132);
  EXPECT_TRUE(valid(r));
  EXPECT_EQ(132u, r.uptime_s);
  EXPECT_EQ(41u, r.boot);

  RetainedUptime noise{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  EXPECT_FALSE(valid(noise));
  RetainedUptime zero{};
  EXPECT_FALSE(valid(zero));
}

TEST(UptimeRtcRecord, TornWritesFailValidation) {
  RetainedUptime before{};
  stamp(before, 41, 131);
  RetainedUptime after{};
  stamp(after, 41, 132);

  RetainedUptime torn = before;   // uptime written, check not yet
  torn.uptime_s = after.uptime_s;
  EXPECT_FALSE(valid(torn));

  torn = before;                  // check written, uptime not yet
  torn.check = after.check;
  EXPECT_FALSE(valid(torn));

  torn = before;                  // boot changed without its check
  torn.boot = 42;
  EXPECT_FALSE(valid(torn));
}

TEST(UptimeRtcRecord, RefreshedOncePerSecond) {
  EXPECT_TRUE(rtcUpdateDue(0, 0, false));
  EXPECT_FALSE(rtcUpdateDue(999, 0, true));
  EXPECT_TRUE(rtcUpdateDue(1000, 0, true));
  EXPECT_TRUE(rtcUpdateDue(5, 0xFFFFFFFFu - 995, true));   // across a wrap
}

TEST(UptimePrevious, RtcWinsWhenTheImmediatelyPreviousBootWroteIt) {
  RetainedUptime r{};
  stamp(r, 233, 7920);   // boot 233 ran 2 h 12 m, far past the NVS stop point
  const Previous p = pickPrevious(r, 234, 955);
  EXPECT_TRUE(p.from_rtc);
  EXPECT_FALSE(p.at_least);
  EXPECT_EQ(7920u, p.seconds);
}

TEST(UptimePrevious, StaleRecordFromAnOlderBootIsRejected) {
  // Build A wrote the record in boot 233; boot 234 ran other firmware that
  // never touched it; boot 235 is build A again. The record is valid but not
  // the previous boot's, so NVS is used.
  RetainedUptime r{};
  stamp(r, 233, 600);
  const Previous p = pickPrevious(r, 235, 30);
  EXPECT_FALSE(p.from_rtc);
  EXPECT_EQ(30u, p.seconds);
}

TEST(UptimePrevious, NvsAfterPowerLossAndFlaggedAtLeastOnceStopped) {
  RetainedUptime lost{};   // power loss: RTC record gone
  Previous p = pickPrevious(lost, 12, 955);
  EXPECT_FALSE(p.from_rtc);
  EXPECT_TRUE(p.at_least);
  EXPECT_EQ(955u, p.seconds);

  p = pickPrevious(lost, 12, 115);   // died inside the schedule: a real reading
  EXPECT_FALSE(p.at_least);
  EXPECT_EQ(115u, p.seconds);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
