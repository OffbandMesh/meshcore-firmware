// #1072: native tests for UsbLinkTracker, the settle/flap state machine behind
// the [usb] lifecycle lines. Pure logic -- no Arduino, no USB stack.

#include <gtest/gtest.h>
#include <vector>
#include "helpers/UsbLinkTracker.h"

namespace {

struct Seen {
  UsbLinkTracker::Event ev;
  uint16_t flaps;
  uint32_t at;
};

// Feed `up` every 1 ms from `from` for `ms` milliseconds, as the main loop
// would, collecting every non-NONE event.
uint32_t feed(UsbLinkTracker& t, bool up, uint32_t from, uint32_t ms, std::vector<Seen>& out) {
  for (uint32_t i = 0; i < ms; i++) {
    uint16_t f = 0xFFFF;
    UsbLinkTracker::Event e = t.update(up, from + i, &f);
    if (e != UsbLinkTracker::NONE) out.push_back({e, f, from + i});
  }
  return from + ms;
}

}  // namespace

TEST(UsbLinkTracker, NoHostAtBootReportsOneDownAndNoPhantomUp) {
  // HWCDC reads "plugged" at power-on, then drops within a few ms.
  UsbLinkTracker t;
  std::vector<Seen> seen;
  uint32_t now = feed(t, true, 0, 5, seen);
  now = feed(t, false, now, 3000, seen);
  ASSERT_EQ(1u, seen.size());
  EXPECT_EQ(UsbLinkTracker::DOWN, seen[0].ev);
  EXPECT_EQ(0u, seen[0].flaps);
}

TEST(UsbLinkTracker, HostPresentAtBootReportsUpAfterSettle) {
  UsbLinkTracker t;
  std::vector<Seen> seen;
  feed(t, true, 0, 3000, seen);
  ASSERT_EQ(1u, seen.size());
  EXPECT_EQ(UsbLinkTracker::UP, seen[0].ev);
  EXPECT_GE(seen[0].at, UsbLinkTracker::kSettleMs);
}

TEST(UsbLinkTracker, CleanUnplugAndReplugReportDownThenUp) {
  UsbLinkTracker t;
  std::vector<Seen> seen;
  uint32_t now = feed(t, true, 0, 2000, seen);
  now = feed(t, false, now, 2000, seen);
  feed(t, true, now, 2000, seen);
  ASSERT_EQ(3u, seen.size());
  EXPECT_EQ(UsbLinkTracker::UP, seen[0].ev);
  EXPECT_EQ(UsbLinkTracker::DOWN, seen[1].ev);
  EXPECT_EQ(1u, seen[1].flaps);   // the one raw change that became the DOWN
  EXPECT_EQ(UsbLinkTracker::UP, seen[2].ev);
}

TEST(UsbLinkTracker, FastReplugInsideTheWindowIsABlipNotSilence) {
  UsbLinkTracker t;
  std::vector<Seen> seen;
  uint32_t now = feed(t, true, 0, 2000, seen);
  now = feed(t, false, now, 300, seen);   // shorter than the settle time
  feed(t, true, now, 2000, seen);
  ASSERT_EQ(2u, seen.size());
  EXPECT_EQ(UsbLinkTracker::UP, seen[0].ev);
  EXPECT_EQ(UsbLinkTracker::BLIP_UP, seen[1].ev);
  EXPECT_EQ(2u, seen[1].flaps);
}

TEST(UsbLinkTracker, ContinuousFlappingIsReportedAndRateLimited) {
  UsbLinkTracker t;
  std::vector<Seen> seen;
  uint32_t now = feed(t, true, 0, 2000, seen);   // UP
  bool level = false;
  for (int i = 0; i < 6000; i++) {                // flip every 5 ms for 30 s
    now = feed(t, level, now, 5, seen);
    level = !level;
  }
  size_t unstable = 0;
  for (const Seen& s : seen) {
    EXPECT_NE(UsbLinkTracker::DOWN, s.ev);        // never settles, never "down"
    if (s.ev == UsbLinkTracker::UNSTABLE) unstable++;
  }
  EXPECT_GE(unstable, 3u);   // reported, not silent...
  EXPECT_LE(unstable, 4u);   // ...but at most once per 10 s over 30 s
}

TEST(UsbLinkTracker, SettleAfterUnstableReportsTheWholeEpisode) {
  UsbLinkTracker t;
  std::vector<Seen> seen;
  uint32_t now = feed(t, true, 0, 2000, seen);
  bool level = false;
  for (int i = 0; i < 40; i++) {                  // 40 raw changes
    now = feed(t, level, now, 5, seen);
    level = !level;
  }
  feed(t, true, now, 2000, seen);                 // settles back up
  ASSERT_GE(seen.size(), 3u);
  EXPECT_EQ(UsbLinkTracker::UNSTABLE, seen[1].ev);
  EXPECT_EQ(UsbLinkTracker::BLIP_UP, seen.back().ev);
  EXPECT_GE(seen.back().flaps, 40u);              // cumulative, not reset by UNSTABLE
}

TEST(UsbLinkTracker, MillisWrapDoesNotBreakTheSettleTime) {
  UsbLinkTracker t;
  std::vector<Seen> seen;
  const uint32_t start = 0xFFFFFFFFu - 500;       // wraps mid-settle
  feed(t, true, start, 3000, seen);
  ASSERT_EQ(1u, seen.size());
  EXPECT_EQ(UsbLinkTracker::UP, seen[0].ev);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
