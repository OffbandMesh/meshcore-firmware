// Epic I (#1433), I0 (#1674): SD hot-swap (I7, #1703) and the header icon
// (I9, #1705). The R8's kit has no card-detect line, so presence is found by
// asking the card; these pin when it's asked and what the answers mean.
// Owner, 2026-10-21: green while working, red when a card was in use and is
// gone or won't mount, nothing on a board that has never seen one.

#include <gtest/gtest.h>

#include <string>

#include "helpers/gui/SdMonitor.h"

using namespace offband::gui;

namespace {

// Brings a monitor to Working at `t`.
void mountAt(SdMonitor& m, uint32_t t) {
  ASSERT_EQ(SdAction::Mount, m.poll(t));
  m.mountResult(MountOutcome::Ok, t);
}

}  // namespace

TEST(SdMonitor, AsksAtBoot) {
  SdMonitor m;
  EXPECT_EQ(SdAction::Mount, m.poll(0));
}

TEST(SdMonitor, NoCardAtBootShowsNothingAndKeepsLooking) {
  SdMonitor m;
  m.poll(0);
  m.mountResult(MountOutcome::NoCard, 0);
  EXPECT_EQ(SdState::NeverSeen, m.state());
  EXPECT_EQ(SdIcon::None, m.icon());
  EXPECT_EQ(SdAction::None, m.poll(SdMonitor::kRetryMs - 1));
  EXPECT_EQ(SdAction::Mount, m.poll(SdMonitor::kRetryMs));
}

TEST(SdMonitor, NoCardDoesntBackOff) {
  SdMonitor m;
  uint32_t t = 0;
  for (int i = 0; i < 5; i++) {
    ASSERT_EQ(SdAction::Mount, m.poll(t));
    m.mountResult(MountOutcome::NoCard, t);
    t += SdMonitor::kRetryMs;
  }
  EXPECT_EQ(SdMonitor::kRetryMs, m.retryDelayMs());
}

TEST(SdMonitor, AWorkingCardIsGreenAndProbedOnSchedule) {
  SdMonitor m;
  mountAt(m, 0);
  EXPECT_EQ(SdState::Working, m.state());
  EXPECT_EQ(SdIcon::Green, m.icon());
  EXPECT_EQ(SdAction::None, m.poll(SdMonitor::kProbeMs - 1));
  EXPECT_EQ(SdAction::Probe, m.poll(SdMonitor::kProbeMs));
  m.probeResult(true, SdMonitor::kProbeMs);
  EXPECT_EQ(SdAction::None, m.poll(SdMonitor::kProbeMs + 1));
  EXPECT_EQ(SdAction::Probe, m.poll(2 * SdMonitor::kProbeMs));
}

TEST(SdMonitor, APulledCardIsUnmountedThenRed) {
  SdMonitor m;
  mountAt(m, 0);
  ASSERT_EQ(SdAction::Probe, m.poll(SdMonitor::kProbeMs));
  m.probeResult(false, SdMonitor::kProbeMs);
  EXPECT_EQ(SdAction::Unmount, m.poll(SdMonitor::kProbeMs));
  m.unmounted(SdMonitor::kProbeMs);
  EXPECT_EQ(SdState::Removed, m.state());
  EXPECT_EQ(SdIcon::Red, m.icon());
}

TEST(SdMonitor, ACardPutBackIsMountedAndGreenAgain) {
  SdMonitor m;
  mountAt(m, 0);
  m.poll(SdMonitor::kProbeMs);
  m.probeResult(false, SdMonitor::kProbeMs);
  m.poll(SdMonitor::kProbeMs);
  m.unmounted(SdMonitor::kProbeMs);
  const uint32_t t = SdMonitor::kProbeMs;
  EXPECT_EQ(SdAction::None, m.poll(t + SdMonitor::kRetryMs - 1));
  ASSERT_EQ(SdAction::Mount, m.poll(t + SdMonitor::kRetryMs));
  m.mountResult(MountOutcome::Ok, t + SdMonitor::kRetryMs);
  EXPECT_EQ(SdState::Working, m.state());
  EXPECT_EQ(SdIcon::Green, m.icon());
}

TEST(SdMonitor, AStillMissingCardStaysRed) {
  SdMonitor m;
  mountAt(m, 0);
  m.poll(SdMonitor::kProbeMs);
  m.probeResult(false, SdMonitor::kProbeMs);
  m.poll(SdMonitor::kProbeMs);
  m.unmounted(SdMonitor::kProbeMs);
  const uint32_t t = SdMonitor::kProbeMs + SdMonitor::kRetryMs;
  ASSERT_EQ(SdAction::Mount, m.poll(t));
  m.mountResult(MountOutcome::NoCard, t);
  EXPECT_EQ(SdState::Removed, m.state());
  EXPECT_EQ(SdIcon::Red, m.icon());
}

// A card that answers but won't mount isn't retried in a tight loop.
TEST(SdMonitor, AnUnreadableCardIsRedAndBacksOff) {
  SdMonitor m;
  uint32_t t = 0;
  uint32_t last = 0;
  for (int i = 0; i < 8; i++) {
    ASSERT_EQ(SdAction::Mount, m.poll(t)) << i;
    m.mountResult(MountOutcome::Unreadable, t);
    EXPECT_EQ(SdState::Failed, m.state());
    EXPECT_EQ(SdIcon::Red, m.icon());
    EXPECT_GE(m.retryDelayMs(), last);
    EXPECT_LE(m.retryDelayMs(), SdMonitor::kBackoffMaxMs);
    last = m.retryDelayMs();
    EXPECT_EQ(SdAction::None, m.poll(t + m.retryDelayMs() - 1));
    t += m.retryDelayMs();
  }
  EXPECT_EQ(SdMonitor::kBackoffMaxMs, m.retryDelayMs());
}

TEST(SdMonitor, TheBackoffResetsOnceTheCardMounts) {
  SdMonitor m;
  uint32_t t = 0;
  for (int i = 0; i < 4; i++) {
    m.poll(t);
    m.mountResult(MountOutcome::Unreadable, t);
    t += m.retryDelayMs();
  }
  m.poll(t);
  m.mountResult(MountOutcome::Ok, t);
  EXPECT_EQ(SdMonitor::kRetryMs, m.retryDelayMs());
}

TEST(SdMonitor, NothingHappensBetweenAsks) {
  SdMonitor m;
  mountAt(m, 0);
  for (uint32_t t = 1; t < SdMonitor::kProbeMs; t += 97) EXPECT_EQ(SdAction::None, m.poll(t)) << t;
}

TEST(SdMonitor, TheRevisionMovesOnlyOnAChange) {
  SdMonitor m;
  const uint32_t r0 = m.revision();
  m.poll(0);
  m.mountResult(MountOutcome::NoCard, 0);
  EXPECT_EQ(r0, m.revision()) << "no card at boot is no change";
  m.poll(SdMonitor::kRetryMs);
  m.mountResult(MountOutcome::Ok, SdMonitor::kRetryMs);
  const uint32_t r1 = m.revision();
  EXPECT_NE(r0, r1);
  m.poll(SdMonitor::kRetryMs + SdMonitor::kProbeMs);
  m.probeResult(true, SdMonitor::kRetryMs + SdMonitor::kProbeMs);
  EXPECT_EQ(r1, m.revision()) << "a healthy probe is no change";
}

// The clock wraps after 49 days; asking must keep working across it.
TEST(SdMonitor, KeepsAskingAcrossTheClockWrap) {
  SdMonitor m;
  const uint32_t t = 0xFFFFFFFFu - 500;
  mountAt(m, t);
  EXPECT_EQ(SdAction::None, m.poll(t + 1000));
  EXPECT_EQ(SdAction::Probe, m.poll(t + SdMonitor::kProbeMs));
}

// ------------------------------------------------------------ Settings text

TEST(SdText, StatusWords) {
  EXPECT_STREQ("Working", sdStatusText(SdState::Working));
  EXPECT_STREQ("Not detected", sdStatusText(SdState::Removed));
  EXPECT_STREQ("Can't read card", sdStatusText(SdState::Failed));
  EXPECT_STREQ("No card", sdStatusText(SdState::NeverSeen));
}

TEST(SdText, Sizes) {
  char b[16];
  sdSizeText(28900000000ULL, b, sizeof(b));
  EXPECT_STREQ("28.9 GB", b);
  sdSizeText(31800000000ULL, b, sizeof(b));
  EXPECT_STREQ("31.8 GB", b);
  sdSizeText(512000000ULL, b, sizeof(b));
  EXPECT_STREQ("512 MB", b);
  sdSizeText(1000000000ULL, b, sizeof(b));
  EXPECT_STREQ("1.0 GB", b);
  sdSizeText(0, b, sizeof(b));
  EXPECT_STREQ("0 MB", b);
  sdSizeText(999999999ULL, b, sizeof(b));
  EXPECT_STREQ("1.0 GB", b) << "rounds up across the boundary, never \"1000 MB\"";
  sdSizeText(999499999ULL, b, sizeof(b));
  EXPECT_STREQ("999 MB", b);
}

TEST(SdMonitor, AFailingCardPulledOutIsRemovedAndItsBackoffEnds) {
  SdMonitor m;
  uint32_t t = 0;
  for (int i = 0; i < 3; i++) {
    m.poll(t);
    m.mountResult(MountOutcome::Unreadable, t);
    t += m.retryDelayMs();
  }
  ASSERT_GT(m.retryDelayMs(), SdMonitor::kRetryMs);
  m.poll(t);
  m.mountResult(MountOutcome::NoCard, t);
  EXPECT_EQ(SdState::Removed, m.state());
  EXPECT_EQ(SdIcon::Red, m.icon());
  EXPECT_EQ(SdMonitor::kRetryMs, m.retryDelayMs());
}

TEST(SdMonitor, AProbeAnswerWhileNotWorkingIsIgnored) {
  SdMonitor m;
  m.poll(0);
  m.mountResult(MountOutcome::NoCard, 0);
  const uint32_t r = m.revision();
  m.probeResult(false, 10);
  EXPECT_EQ(SdState::NeverSeen, m.state());
  EXPECT_EQ(r, m.revision());
  EXPECT_NE(SdAction::Unmount, m.poll(SdMonitor::kRetryMs));
}

TEST(SdMonitor, AStillMissingCardIsNoNewChange) {
  SdMonitor m;
  mountAt(m, 0);
  m.poll(SdMonitor::kProbeMs);
  m.probeResult(false, SdMonitor::kProbeMs);
  m.poll(SdMonitor::kProbeMs);
  m.unmounted(SdMonitor::kProbeMs);
  const uint32_t r = m.revision();
  m.unmounted(SdMonitor::kProbeMs + 1);   // a second report changes nothing
  uint32_t t = SdMonitor::kProbeMs + SdMonitor::kRetryMs;
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(SdAction::Mount, m.poll(t));
    m.mountResult(MountOutcome::NoCard, t);
    t += SdMonitor::kRetryMs;
  }
  EXPECT_EQ(r, m.revision()) << "Removed -> Removed redraws the screen for nothing";
}

TEST(SdText, ATooSmallBufferIsCutNotOverrun) {
  char b[5];
  memset(b, 'x', sizeof(b));
  sdSizeText(28900000000ULL, b, 4);
  EXPECT_EQ('\0', b[3]);
  EXPECT_EQ('x', b[4]);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
