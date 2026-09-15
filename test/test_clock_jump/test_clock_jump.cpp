// Native unit tests for noticing the clock being set (#1233): the owner's badge ran
// 19 hours behind until his phone set it, and times stamped before then were off by it.

#include <gtest/gtest.h>
#include "helpers/ClockJump.h"

using offband::ClockJump;

namespace {
constexpr uint32_t kWall = 1789420000;   // 2026-09-14, a stale contact time
}  // namespace

TEST(ClockJump, TheFirstCheckOnlyReads) {
  ClockJump j;
  EXPECT_EQ(0, j.check(kWall, 5000));
}

// A clock that keeps pace with millis is no jump, whether checks come milliseconds or
// seconds apart, and whichever way the seconds round.
TEST(ClockJump, KeepingPaceIsNoJump) {
  ClockJump j;
  j.check(kWall, 5000);
  EXPECT_EQ(0, j.check(kWall, 5400));
  EXPECT_EQ(0, j.check(kWall + 1, 5999));   // the second ticked over; millis says 0 s
  EXPECT_EQ(0, j.check(kWall + 11, 15999));  // a loop held up for 10 s
}

TEST(ClockJump, ThePhoneSettingItForwardIsAJump) {
  ClockJump j;
  j.check(kWall, 5000);
  EXPECT_EQ(19 * 3600, j.check(kWall + 19 * 3600, 5020));
  EXPECT_EQ(0, j.check(kWall + 19 * 3600 + 1, 6020));   // and after it, pace again
}

TEST(ClockJump, SettingItBackIsAJump) {
  ClockJump j;
  j.check(kWall, 5000);
  EXPECT_EQ(-3600, j.check(kWall - 3600, 5020));
}

TEST(ClockJump, SmallCorrectionsAreLetGo) {
  ClockJump j;
  j.check(kWall, 5000);
  EXPECT_EQ(0, j.check(kWall + 4, 5020));   // a GPS resync trimming drift
  EXPECT_EQ(6, j.check(kWall + 10, 5040));
}

// millis wraps after 49.7 days; the elapsed time across it still counts.
TEST(ClockJump, MillisWrappingIsNoJump) {
  ClockJump j;
  j.check(kWall, 0xFFFFFC18u);                // 1 s before the wrap
  EXPECT_EQ(0, j.check(kWall + 2, 1000));     // 2 s later, past it
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
