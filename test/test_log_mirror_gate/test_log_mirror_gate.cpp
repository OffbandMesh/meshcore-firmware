// #2324: the side-port log mirror's send / probe / resume decisions.
// The defect this guards: once five bytes in a row timed out, putc() returned
// before trying again, so the wire stayed off for the rest of the boot. These
// tests pin that a down wire is probed, comes back, and reports what it lost.

#include <gtest/gtest.h>
#include <limits.h>
#include <string.h>

#include "helpers/LogMirrorGate.h"

using namespace offband;

namespace {

constexpr uint8_t kGiveup = 5;

// Drive the gate the way a back end does for one byte: ask for the step, then
// report the outcome. `ready` is whether the wire could take the byte.
// Returns true when the byte was sent.
bool byte(LogMirrorGate& g, bool ready, uint16_t probe_every = 1, uint32_t status = 0) {
  const MirrorStep s = mirrorNextStep(g, kGiveup, probe_every);
  if (s == MirrorStep::Skip) return false;
  if (!ready) {
    mirrorOnTimeout(g, kGiveup, status);
    return false;
  }
  mirrorOnSent(g);
  return true;
}

void stall(LogMirrorGate& g, uint32_t status = 0xABCD0000) {
  for (int i = 0; i < kGiveup; i++) byte(g, false, 1, status + i);
}

}  // namespace

TEST(Gate, ZeroInitIsUp) {
  LogMirrorGate g = {};
  EXPECT_FALSE(mirrorDown(g, kGiveup));
  EXPECT_EQ(mirrorNextStep(g, kGiveup, 1), MirrorStep::Send);
}

TEST(Gate, HealthyWireSendsEveryByteAndOwesNoNotice) {
  LogMirrorGate g = {};
  for (int i = 0; i < 1000; i++) ASSERT_TRUE(byte(g, true));
  EXPECT_EQ(g.dropped, 0u);
  EXPECT_EQ(g.stalls, 0u);
  EXPECT_FALSE(mirrorTakeResume(g).pending);
}

TEST(Gate, GoesDownOnlyAtTheGiveupCount) {
  LogMirrorGate g = {};
  for (int i = 0; i < kGiveup - 1; i++) byte(g, false);
  EXPECT_FALSE(mirrorDown(g, kGiveup));
  EXPECT_EQ(g.stalls, 0u);
  byte(g, false);
  EXPECT_TRUE(mirrorDown(g, kGiveup));
  EXPECT_EQ(g.stalls, 1u);
  EXPECT_EQ(g.dropped, uint32_t(kGiveup));
}

// The #2324 regression: before the fix a down wire was never tried again.
TEST(Gate, DownWireIsProbedAndComesBack) {
  LogMirrorGate g = {};
  stall(g);
  ASSERT_TRUE(mirrorDown(g, kGiveup));
  EXPECT_EQ(mirrorNextStep(g, kGiveup, 1), MirrorStep::Probe);
  mirrorOnSent(g);
  EXPECT_FALSE(mirrorDown(g, kGiveup));
  EXPECT_EQ(mirrorNextStep(g, kGiveup, 1), MirrorStep::Send);   // full sends again
}

TEST(Gate, StillStalledProbeSendsNothingAndCountsTheLoss) {
  LogMirrorGate g = {};
  stall(g);
  for (int i = 0; i < 100; i++) EXPECT_FALSE(byte(g, false));
  EXPECT_TRUE(mirrorDown(g, kGiveup));
  EXPECT_EQ(g.stalls, 1u);                        // one stall, not 101
  EXPECT_EQ(g.dropped, uint32_t(kGiveup + 100));
  EXPECT_FALSE(g.resume_pending);
}

TEST(Gate, ResumeNoticeCarriesWhatWasLost) {
  LogMirrorGate g = {};
  byte(g, true);
  stall(g, 0x00100000);
  for (int i = 0; i < 7; i++) byte(g, false, 1, 0xFFFFFFFF);   // failed probes
  ASSERT_TRUE(byte(g, true));
  const MirrorResume r = mirrorTakeResume(g);
  EXPECT_TRUE(r.pending);
  EXPECT_EQ(r.dropped, uint32_t(kGiveup + 7));
  EXPECT_EQ(r.stalls, 1u);
  EXPECT_EQ(r.status, 0x00100000u);               // the first lost byte's status
}

TEST(Gate, NoticeIsTakenOnceAndTheCountRestarts) {
  LogMirrorGate g = {};
  stall(g);
  byte(g, true);
  EXPECT_TRUE(mirrorTakeResume(g).pending);
  EXPECT_FALSE(mirrorTakeResume(g).pending);
  EXPECT_EQ(g.dropped, 0u);
  byte(g, true);
  EXPECT_FALSE(mirrorTakeResume(g).pending);      // no new loss, no notice
}

TEST(Gate, ALossShortOfAStallIsStillReported) {
  LogMirrorGate g = {};
  byte(g, false, 1, 0x42);
  byte(g, false, 1, 0x43);
  ASSERT_TRUE(byte(g, true));
  const MirrorResume r = mirrorTakeResume(g);
  EXPECT_TRUE(r.pending);
  EXPECT_EQ(r.dropped, 2u);
  EXPECT_EQ(r.stalls, 0u);
  EXPECT_EQ(r.status, 0x42u);
}

TEST(Gate, EachStallIsCountedAndKeepsItsOwnStatus) {
  LogMirrorGate g = {};
  stall(g, 0x1000);
  byte(g, true);
  EXPECT_EQ(mirrorTakeResume(g).status, 0x1000u);
  stall(g, 0x2000);
  byte(g, true);
  const MirrorResume r = mirrorTakeResume(g);
  EXPECT_EQ(r.stalls, 2u);
  EXPECT_EQ(r.status, 0x2000u);
  EXPECT_EQ(r.dropped, uint32_t(kGiveup));
}

TEST(Gate, ProbesEveryNthByteWhileDown) {
  LogMirrorGate g = {};
  stall(g);
  const uint32_t before = g.dropped;
  int probes = 0;
  for (int i = 0; i < 64 * 4; i++) {
    const MirrorStep s = mirrorNextStep(g, kGiveup, 64);
    ASSERT_NE(s, MirrorStep::Send);
    if (s == MirrorStep::Probe) {
      probes++;
      mirrorOnTimeout(g, kGiveup, 0);
    }
  }
  EXPECT_EQ(probes, 4);
  EXPECT_EQ(g.dropped - before, 64u * 4u);         // every byte, skipped or probed
}

TEST(Gate, AnEveryNthProbeStillBringsItBack) {
  LogMirrorGate g = {};
  stall(g);
  int sent_at = -1;
  for (int i = 0; i < 200 && sent_at < 0; i++)
    if (byte(g, true, 64)) sent_at = i;
  EXPECT_EQ(sent_at, 63);                          // the 64th byte is the probe
  EXPECT_FALSE(mirrorDown(g, kGiveup));
}

// The widest interval: the counter reaches the interval and resets, so it
// never wraps (Gemini review, #2324).
TEST(Gate, WidestProbeIntervalProbesOnItsLastByte) {
  LogMirrorGate g = {};
  stall(g);
  for (uint32_t i = 1; i < UINT16_MAX; i++)
    ASSERT_EQ(mirrorNextStep(g, kGiveup, UINT16_MAX), MirrorStep::Skip) << i;
  EXPECT_EQ(mirrorNextStep(g, kGiveup, UINT16_MAX), MirrorStep::Probe);
  EXPECT_EQ(g.since_probe, 0u);
  EXPECT_EQ(mirrorNextStep(g, kGiveup, UINT16_MAX), MirrorStep::Skip);
}

TEST(Format, ExactNotice) {
  const MirrorResume r = {true, 3, 1234, 0x0001c0deu};
  char out[96];
  const size_t n = mirrorFormatResume(out, sizeof out, r);
  EXPECT_STREQ(out, "\r\n[mirror] resumed: dropped=1234 stalls=3 status=0x0001c0de\r\n");
  EXPECT_EQ(n, strlen(out));
}

TEST(Format, LargestValuesFitAndZerosPrint) {
  char out[96];
  MirrorResume r = {true, UINT32_MAX, UINT32_MAX, UINT32_MAX};
  EXPECT_GT(mirrorFormatResume(out, sizeof out, r), 0u);
  EXPECT_STREQ(out, "\r\n[mirror] resumed: dropped=4294967295 stalls=4294967295 status=0xffffffff\r\n");
  r = {true, 0, 0, 0};
  mirrorFormatResume(out, sizeof out, r);
  EXPECT_STREQ(out, "\r\n[mirror] resumed: dropped=0 stalls=0 status=0x00000000\r\n");
}

TEST(Format, TooSmallABufferWritesNothing) {
  const MirrorResume r = {true, 1, 1, 0};
  char out[8] = "keep";
  EXPECT_EQ(mirrorFormatResume(out, sizeof out, r), 0u);
  EXPECT_STREQ(out, "keep");
  EXPECT_EQ(mirrorFormatResume(nullptr, 64, r), 0u);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
