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

// A ladder mark, as the schedule writes it.
static NvsUptime ladder(uint32_t s) { return decodeNvs(encodeNvs(s, false)); }
// A shutdown flush, which knows the runtime exactly.
static NvsUptime flushed(uint32_t s) { return decodeNvs(encodeNvs(s, true)); }

TEST(UptimePrevious, RtcWinsWhenTheImmediatelyPreviousBootWroteIt) {
  RetainedUptime r{};
  stamp(r, 233, 7920);   // boot 233 ran 2 h 12 m, far past the NVS stop point
  const Previous p = pickPrevious(r, 234, ladder(955));
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
  const Previous p = pickPrevious(r, 235, ladder(30));
  EXPECT_FALSE(p.from_rtc);
  EXPECT_EQ(30u, p.seconds);
}

TEST(UptimePrevious, EveryLadderValueIsAFloorNotAReading) {
  // #1272, from #1076 Run A: rc32-bench-1 ran 734 s and the line said "60s".
  // The ladder had written 60 and stopped, so 60 is a floor -- but only values
  // past the LAST mark were flagged, and a 60 s floor printed as a measurement.
  // A tester reads that as a board rebooting after a minute.
  RetainedUptime lost{};   // power loss, or a CHIP_PU reset: RTC record gone
  for (uint32_t mark_s : {1u, 60u, 900u}) {
    const Previous p = pickPrevious(lost, 12, ladder(mark_s));
    EXPECT_FALSE(p.from_rtc) << "mark " << mark_s;
    EXPECT_TRUE(p.at_least)  << "mark " << mark_s;
    EXPECT_EQ(mark_s, p.seconds);
  }
}

TEST(UptimePrevious, AShutdownFlushIsExactAndSaysSo) {
  // The other writer on the same key: the board went down on purpose and
  // recorded its runtime (#1270). That is a measurement, not a floor, and
  // must not grow a "+".
  RetainedUptime lost{};
  const Previous p = pickPrevious(lost, 12, flushed(734));
  EXPECT_FALSE(p.from_rtc);
  EXPECT_FALSE(p.at_least);
  EXPECT_EQ(734u, p.seconds);
}

TEST(UptimePrevious, NothingEverWrittenIsNotAFloor) {
  // Zero means no record, not "ran at least zero seconds".
  RetainedUptime lost{};
  const Previous p = pickPrevious(lost, 12, ladder(0));
  EXPECT_FALSE(p.at_least);
  EXPECT_EQ(0u, p.seconds);
}

TEST(UptimeNvsEncoding, ARecordFromBeforeTheBitReadsAsAFloor) {
  // Firmware that predates #1272 stored a bare second count. Its provenance is
  // unknown, and a floor is the safe reading: it never overstates the runtime.
  const NvsUptime legacy = decodeNvs(734u);
  EXPECT_EQ(734u, legacy.seconds);
  EXPECT_FALSE(legacy.exact);

  RetainedUptime lost{};
  EXPECT_TRUE(pickPrevious(lost, 12, legacy).at_least);
}

TEST(UptimePrevious, AFrozenNvsBootCounterDoesNotDiscardAGoodRtcRecord) {
  // The record is bound to the RTC boot counter, which advances in the same
  // power domain as the record itself. It used to be bound to the NVS counter:
  // an NVS 'count' write that fails leaves the stored count frozen, the next
  // boot computes the SAME number, "previous + 1" never matches, and an exact
  // record is thrown away for a stale NVS value from an older boot -- which can
  // OVERSTATE the previous runtime and hide the crash cycle it exists to show.
  RetainedUptime r{};
  stamp(r, 9, 734);                       // RTC counter reached 9; ran 734 s
  const Previous p = pickPrevious(r, 10, ladder(900));   // RTC counter advanced
  EXPECT_TRUE(p.from_rtc);
  EXPECT_FALSE(p.at_least);
  EXPECT_EQ(734u, p.seconds) << "the stale 900 s floor would have overstated it";
}

TEST(UptimePrevious, ABootCounterAtTheTopOfTheRangeDoesNotWrapIntoAMatch) {
  // The counter restarts at 1 when its own magic fails. UINT32_MAX + 1 is also
  // 1, so a record stamped at the top of the range would otherwise be accepted
  // by a counter that had just restarted -- an exact-looking runtime from
  // billions of boots ago.
  RetainedUptime r{};
  stamp(r, 0xFFFFFFFFu, 4242);
  const Previous p = pickPrevious(r, 1, ladder(60));
  EXPECT_FALSE(p.from_rtc);
  EXPECT_EQ(60u, p.seconds);
}

TEST(UptimePrevious, ABuildThatSkipsAGenerationIsRejected) {
  // What boot-binding is for: our build wrote the record, a DIFFERENT Offband
  // build ran next, ours runs again. That build still advances the boot counter
  // (it lives in heartbeatBegin, not in this feature), so the gap shows up as a
  // skipped generation and the stale record is refused.
  //
  // The case this CANNOT see is foreign firmware that advances no counter at
  // all and leaves our RTC structs byte-for-byte intact: the record then looks
  // like the immediately previous boot's. No counter can detect that, and it
  // was equally true when the record was bound to the NVS count. In practice
  // foreign firmware reuses the RTC region and the record fails validation.
  RetainedUptime r{};
  stamp(r, 5, 7920);
  const Previous p = pickPrevious(r, 7, ladder(60));   // 5 -> 7: a boot missing
  EXPECT_FALSE(p.from_rtc);
  EXPECT_EQ(60u, p.seconds);
}

TEST(UptimeNvsEncoding, AGarbledRecordIsNoRecordRatherThanAConfidentLie) {
  // The stored value has no magic and no check word. Without a bound, NVS
  // corruption decodes into a precise-looking figure: 0xDEADBEEF has the top
  // bit set, so it would read as an EXACT 1.6-billion-second runtime.
  const NvsUptime garbled = decodeNvs(0xDEADBEEFu);
  EXPECT_EQ(0u, garbled.seconds);
  EXPECT_FALSE(garbled.exact);

  RetainedUptime lost{};
  const Previous p = pickPrevious(lost, 12, garbled);
  EXPECT_FALSE(p.at_least);
  EXPECT_EQ(0u, p.seconds);

  // The bound itself is inclusive, and a year of uptime is still plausible.
  EXPECT_EQ(kMaxPlausibleUptimeS, decodeNvs(encodeNvs(kMaxPlausibleUptimeS, true)).seconds);
  EXPECT_EQ(0u, decodeNvs(encodeNvs(kMaxPlausibleUptimeS + 1u, true)).seconds);
  EXPECT_EQ(31536000u, decodeNvs(encodeNvs(31536000u, false)).seconds);
}

TEST(UptimeNvsEncoding, TheFlagSurvivesTheRoundTripAndKeepsTheSeconds) {
  for (uint32_t s : {0u, 1u, 60u, 734u, 900u, 86400u, kMaxPlausibleUptimeS}) {
    EXPECT_EQ(s, decodeNvs(encodeNvs(s, false)).seconds);
    EXPECT_EQ(s, decodeNvs(encodeNvs(s, true)).seconds);
    EXPECT_FALSE(decodeNvs(encodeNvs(s, false)).exact);
    EXPECT_TRUE(decodeNvs(encodeNvs(s, true)).exact);
  }
  // The flag is the top bit, so the two encodings differ by exactly that bit.
  EXPECT_EQ(kNvsExactBit, encodeNvs(734, true) ^ encodeNvs(734, false));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
