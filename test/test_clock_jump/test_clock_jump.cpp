// Native unit tests for noticing the clock being set (#1233): the owner's badge ran
// 19 hours behind until his phone set it, and times stamped before then were off by it.

#include <gtest/gtest.h>
#include "helpers/ClockJump.h"

using offband::ClockJump;

namespace {
constexpr uint32_t kWall = 1789420000;   // 2026-09-14, a stale contact time
constexpr int64_t kSet = 19 * 3600;
}  // namespace

TEST(ClockJump, TheFirstCheckOnlyReads) {
  ClockJump j;
  EXPECT_EQ(0, j.check(kWall, 5000).by);
}

// A clock that keeps pace with millis is no jump, whether checks come milliseconds or
// seconds apart, and whichever way the seconds round.
TEST(ClockJump, KeepingPaceIsNoJump) {
  ClockJump j;
  j.check(kWall, 5000);
  EXPECT_EQ(0, j.check(kWall, 5400).by);
  EXPECT_EQ(0, j.check(kWall + 1, 5999).by);   // the second ticked over; millis says 0 s
  EXPECT_EQ(0, j.check(kWall + 11, 15999).by);  // a loop held up for 10 s
}

TEST(ClockJump, ThePhoneSettingItForwardIsAJump) {
  ClockJump j;
  j.check(kWall, 5000);
  EXPECT_EQ(kSet, j.check(kWall + kSet, 5020).by);
  EXPECT_EQ(0, j.check(kWall + kSet + 1, 6020).by);   // and after it, pace again
}

TEST(ClockJump, SettingItBackIsAJump) {
  ClockJump j;
  j.check(kWall, 5000);
  EXPECT_EQ(-3600, j.check(kWall - 3600, 5020).by);
}

TEST(ClockJump, SmallCorrectionsAreLetGo) {
  ClockJump j;
  j.check(kWall, 5000);
  EXPECT_EQ(0, j.check(kWall + 4, 5020).by);   // a GPS resync trimming drift
  EXPECT_EQ(6, j.check(kWall + 10, 5040).by);
}

// millis wraps after 49.7 days; the elapsed time across it still counts.
TEST(ClockJump, MillisWrappingIsNoJump) {
  ClockJump j;
  j.check(kWall, 0xFFFFFC18u);                    // 1 s before the wrap
  EXPECT_EQ(0, j.check(kWall + 2, 1000).by);      // 2 s later, past it
}

// The stamps to move are this run's: from the first reading up to the moment before
// the set. A contact saved on an earlier run is older than the first reading, since
// MeshCore starts the clock just past the newest saved one.
TEST(ClockJump, AJumpCoversThisRunsStamps) {
  ClockJump j;
  j.check(kWall, 5000);
  const ClockJump::Jump jump = j.check(kWall + kSet + 600, 605000);   // set 10 min into the run
  EXPECT_EQ(kSet, jump.by);
  EXPECT_TRUE(jump.covers(kWall));             // heard at the start of the run
  EXPECT_TRUE(jump.covers(kWall + 599));       // a second before the set
  EXPECT_FALSE(jump.covers(kWall - 1));        // saved on an earlier run
  EXPECT_FALSE(jump.covers(kWall + kSet + 600));   // stamped after the set
}

// Stamps moved by one jump are still this run's at the next.
TEST(ClockJump, ASecondJumpCoversWhatTheFirstMoved) {
  ClockJump j;
  j.check(kWall, 5000);
  j.check(kWall + kSet, 5020);
  const ClockJump::Jump again = j.check(kWall + kSet - 7200, 6020);   // set back 2 h
  EXPECT_EQ(-7201, again.by);
  EXPECT_TRUE(again.covers(kWall + kSet));   // a stamp the first jump moved
  EXPECT_FALSE(again.covers(kWall - 1));
}

TEST(ClockJump, MovedStopsAtTheEnds) {
  EXPECT_EQ(0u, ClockJump::moved(100, -500));
  EXPECT_EQ(0xFFFFFFFFu, ClockJump::moved(0xFFFFFF00u, 1000));
  EXPECT_EQ(1100u, ClockJump::moved(100, 1000));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
