// Native unit tests for spotting repeats of the badge's own channel sends (#1232).
// A repeater passes a flood packet on unchanged, so its hash is the hash of the send.

#include <gtest/gtest.h>
#include "helpers/HeardRepeats.h"

using Repeats = offband::HeardRepeats<3, 8>;   // 3 sends watched, 8-byte hashes

namespace {
struct Hash {
  uint8_t b[8];
  explicit Hash(uint8_t v) { memset(b, v, sizeof(b)); }
};
constexpr uint32_t kT = 1000;   // any millis() reading
}  // namespace

TEST(HeardRepeats, ARepeatNamesItsSend) {
  Repeats r;
  r.watch(Hash(1).b, 101, kT);
  r.watch(Hash(2).b, 102, kT);
  EXPECT_EQ(102u, r.heard(Hash(2).b, kT + 50));
  EXPECT_EQ(101u, r.heard(Hash(1).b, kT + 50));
}

TEST(HeardRepeats, OtherTrafficIsNotOurs) {
  Repeats r;
  r.watch(Hash(1).b, 101, kT);
  EXPECT_EQ(0u, r.heard(Hash(9).b, kT));
}

// Every repeater that passes it on is another repeat: the same answer each time.
TEST(HeardRepeats, EveryRepeatAnswersTheSame) {
  Repeats r;
  r.watch(Hash(1).b, 101, kT);
  EXPECT_EQ(101u, r.heard(Hash(1).b, kT + 10));
  EXPECT_EQ(101u, r.heard(Hash(1).b, kT + 20));
}

TEST(HeardRepeats, TheOldestSendMakesRoom) {
  Repeats r;
  r.watch(Hash(1).b, 101, kT);
  r.watch(Hash(2).b, 102, kT);
  r.watch(Hash(3).b, 103, kT);
  r.watch(Hash(4).b, 104, kT);   // three slots: the first send is forgotten
  EXPECT_EQ(0u, r.heard(Hash(1).b, kT));
  EXPECT_EQ(104u, r.heard(Hash(4).b, kT));
  EXPECT_EQ(102u, r.heard(Hash(2).b, kT));
}

// Repeats come within seconds. After the window nothing is pending, so logRx() has
// no reason to hash what it hears.
TEST(HeardRepeats, AWatchEndsAfterItsWindow) {
  Repeats r;
  r.watch(Hash(1).b, 101, kT);
  EXPECT_TRUE(r.watching(kT + Repeats::kWatchMs - 1));
  EXPECT_EQ(101u, r.heard(Hash(1).b, kT + Repeats::kWatchMs - 1));
  EXPECT_FALSE(r.watching(kT + Repeats::kWatchMs));
  EXPECT_EQ(0u, r.heard(Hash(1).b, kT + Repeats::kWatchMs));
}

TEST(HeardRepeats, TheWindowSurvivesMillisWrapping) {
  Repeats r;
  const uint32_t before_wrap = 0xFFFFFF00u;
  r.watch(Hash(1).b, 101, before_wrap);
  EXPECT_EQ(101u, r.heard(Hash(1).b, 0x00000100u));   // 512 ms later, past the wrap
}

TEST(HeardRepeats, NothingWatchedMatchesNothing) {
  Repeats r;
  EXPECT_FALSE(r.watching(kT));
  EXPECT_EQ(0u, r.heard(Hash(0).b, kT));   // an all-zero hash must not match empty slots
}

TEST(HeardRepeats, IdZeroIsNeverWatched) {
  Repeats r;
  r.watch(Hash(1).b, 0, kT);
  EXPECT_EQ(0u, r.heard(Hash(1).b, kT));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
