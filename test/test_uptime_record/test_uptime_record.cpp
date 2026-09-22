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

TEST(UptimeNvsSchedule, NothingSavesBeforeTheFirstMark) {
  EXPECT_FALSE(nvsSaveDue(0, 0, false));
  EXPECT_FALSE(nvsSaveDue(kNvsMarks[0] - 1, 0, false));
  EXPECT_TRUE(nvsSaveDue(kNvsMarks[0], 0, false));
}

TEST(UptimeNvsSchedule, SavesExactlyAtTheMarksAndNowhereElse) {
  const std::vector<uint32_t> saves = runNvs(0, 60u * 60u * 1000u);   // one hour
  ASSERT_EQ(kNvsMarkCount, saves.size());
  for (size_t i = 0; i < kNvsMarkCount; i++) EXPECT_EQ(kNvsMarks[i], saves[i]);
}

TEST(UptimeNvsSchedule, WritesPerBootAreBoundedRegardlessOfUptime) {
  const size_t one_hour = runNvs(0, 60u * 60u * 1000u).size();
  const size_t three_hours = runNvs(0, 3u * 60u * 60u * 1000u).size();
  EXPECT_EQ(one_hour, three_hours);   // stopped: uptime no longer costs writes
  EXPECT_EQ(kNvsMarkCount, one_hour);
}

TEST(UptimeNvsSchedule, AFlushCountsAsTheSaveForEveryMarkItPasses) {
  // crashLogUptimeFlush() saves at an arbitrary time (a shutdown at 5 min) and
  // records it as the last save. No mark already passed is written again; the
  // next one still is.
  const uint32_t flush_at = 5u * 60u * 1000u;
  EXPECT_FALSE(nvsSaveDue(flush_at + 1000, flush_at, true));
  EXPECT_FALSE(nvsSaveDue(kNvsStopAfterMs - 1, flush_at, true));
  EXPECT_TRUE(nvsSaveDue(kNvsStopAfterMs, flush_at, true));
}

TEST(UptimeNvsSchedule, AFailedWriteLeavesTheMarkUnspent) {
  // The caller only updates last/saved when the write landed, so a mark that
  // failed stays due and the next tick retries it.
  EXPECT_TRUE(nvsSaveDue(kNvsMarks[0], 0, false));
  EXPECT_TRUE(nvsSaveDue(kNvsMarks[0] + 5000, 0, false));   // still unsaved: still due
}

TEST(UptimeNvsSchedule, ALateTickStillSavesOnceForThePassedMark) {
  // A loop that stalls past a mark saves once when it comes back, not twice.
  EXPECT_TRUE(nvsSaveDue(90u * 1000u, 1000, true));    // 1 min mark passed
  EXPECT_FALSE(nvsSaveDue(90u * 1000u, 61000, true));  // already covered
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
