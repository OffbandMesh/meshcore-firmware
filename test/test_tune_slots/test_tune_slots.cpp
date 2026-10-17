// Native unit tests for the custom tune slot format (#1474).
//
// The id space, the derived name and the line format. The file I/O and the staging
// buffer live in the badge's store and need a filesystem; what is testable here is every
// rule that decides what a slot MEANS, which is where a mistake would be silent.
//
// The staging buffer exists because `NonBlockingRtttl` keeps the melody POINTER --
// `buffer = iSongBuffer` at NonBlockingRtttl.cpp:150 -- and walks it across successive
// loop() calls. A slot read into a stack buffer would dangle the moment the reader
// returned, and would have passed any test whose tune finished before the stack was
// reused. That is why nothing here hands out a transient pointer.

#include <gtest/gtest.h>
#include <cstring>
#include <set>

#include <helpers/ui/TuneSlots.h>

using namespace offband;

// ---- the id space ---------------------------------------------------------------------

// Slots must start exactly where the builtins end. A builtin added without moving
// kFirstSlotId would alias a slot onto it -- the static_assert in the header catches the
// compile-time half; this pins the runtime predicate.
TEST(TuneSlotIds, StartWhereTheBuiltinsEnd) {
  EXPECT_EQ(kBuiltinCount, kFirstSlotId);
  EXPECT_FALSE(isSlotId((uint8_t)(kFirstSlotId - 1))) << "the last builtin is not a slot";
  EXPECT_TRUE(isSlotId(kFirstSlotId));
  EXPECT_TRUE(isSlotId((uint8_t)(kFirstSlotId + kSlotCount - 1)));
  EXPECT_FALSE(isSlotId((uint8_t)(kFirstSlotId + kSlotCount))) << "one past the last slot";
}

// No builtin id may be mistaken for a slot, or choosing a shipped tune would read a file.
TEST(TuneSlotIds, NoBuiltinIsASlot) {
  for (uint8_t id = 0; id < kBuiltinCount; id++) {
    EXPECT_FALSE(isSlotId(id)) << "builtin id " << (int)id << " reads as a slot";
  }
}

TEST(TuneSlotIds, TheUnsetSentinelIsNotASlot) {
  EXPECT_FALSE(isSlotId(kTuneUnset))
      << "0xFF means 'nothing chosen yet' and must never resolve to a slot";
}

TEST(TuneSlotIds, IndexAndIdRoundTrip) {
  std::set<int> ids;
  for (uint8_t i = 0; i < kSlotCount; i++) {
    const uint8_t id = slotIdAt(i);
    EXPECT_TRUE(isSlotId(id));
    EXPECT_EQ(i, slotIndex(id));
    ids.insert(id);
  }
  EXPECT_EQ((size_t)kSlotCount, ids.size()) << "two slots share an id";
}

// ---- the derived name ----------------------------------------------------------------

TEST(SlotName, IsTheRtttlsOwnNameField) {
  char out[32];
  ASSERT_EQ(5u, deriveSlotName("Nokia:d=4,o=5,b=63:8e6,8d6", out, sizeof(out)));
  EXPECT_STREQ("Nokia", out);
}

// A string with no colon is not playable -- rtttlProblem() refuses it because the player
// walks past the buffer hunting for one -- so it has no name either. Reporting a name for
// an unplayable string would put a row in the picker that can never make a sound.
TEST(SlotName, AStringWithNoColonHasNoName) {
  char out[32];
  EXPECT_EQ(0u, deriveSlotName("NokiaWithNoColon", out, sizeof(out)));
  EXPECT_STREQ("", out);
}

TEST(SlotName, AnEmptyOrNullStringHasNoName) {
  char out[32] = {'Z', 0};
  EXPECT_EQ(0u, deriveSlotName("", out, sizeof(out)));
  EXPECT_STREQ("", out) << "an empty slot must clear the caller's buffer, not leave it stale";
  EXPECT_EQ(0u, deriveSlotName(nullptr, out, sizeof(out)));
  EXPECT_STREQ("", out);
}

// A name field that is only a colon, or only whitespace before one, is empty rather than
// a row of blanks in the picker.
TEST(SlotName, AnEmptyNameFieldIsNoName) {
  char out[32];
  EXPECT_EQ(0u, deriveSlotName(":d=4,o=5,b=63:8e6", out, sizeof(out)));
  EXPECT_EQ(0u, deriveSlotName("   :d=4,o=5,b=63:8e6", out, sizeof(out)));
  EXPECT_STREQ("", out);
}

TEST(SlotName, TrimsSpaceAroundTheName) {
  char out[32];
  EXPECT_EQ(5u, deriveSlotName("  Nokia  :d=4,o=5,b=63:8e6", out, sizeof(out)));
  EXPECT_STREQ("Nokia", out);
}

// The row is label-plus-value on 21 columns, so a long name is truncated rather than
// allowed to push the value off the screen.
TEST(SlotName, TruncatesToWhatARowCanShow) {
  char out[64];
  const size_t n = deriveSlotName("AVeryLongRingtoneNameIndeed:d=4,o=5,b=63:8e6",
                                  out, sizeof(out));
  EXPECT_EQ(kMaxSlotName, n);
  EXPECT_EQ(kMaxSlotName, strlen(out));
  EXPECT_EQ(0, strncmp("AVeryLongRin", out, kMaxSlotName));
}

TEST(SlotName, RespectsASmallerCallerBuffer) {
  char out[4];
  EXPECT_EQ(3u, deriveSlotName("Nokia:d=4:8e6", out, sizeof(out)));
  EXPECT_STREQ("Nok", out);
  char none[1] = {'Z'};
  EXPECT_EQ(0u, deriveSlotName("Nokia:d=4:8e6", none, 0));
  EXPECT_EQ('Z', none[0]) << "a zero-capacity buffer must not be written";
}

// #1474 review, the real bug this pins. The store reads only a PREFIX of a slot file to
// get its name, and the first version read 13 characters. A name of 13 or more pushes its
// colon outside that prefix, so deriveSlotName() correctly finds no colon and reports no
// name -- and the slot then rendered as EMPTY, making a saved tune invisible.
//
// deriveSlotName is right in both cases; the caller's prefix was too small. This asserts
// the mechanism, so the store's 64-byte prefix cannot be quietly shrunk back.
TEST(SlotName, APrefixTooShortForTheColonYieldsNoName) {
  const char* full = "MyLongTuneName:d=4,o=5,b=140:8g#";
  char out[32];

  // 13 characters: the colon is at index 14, outside the prefix. No name -- the bug.
  char tight[14];
  memcpy(tight, full, 13);
  tight[13] = '\0';
  EXPECT_EQ(0u, deriveSlotName(tight, out, sizeof(out)))
      << "a prefix that stops before the colon must not invent a name";

  // A prefix wide enough to contain the colon names it, truncated for the row.
  char wide[65];
  const size_t n = strlen(full) < 64 ? strlen(full) : 64;
  memcpy(wide, full, n);
  wide[n] = '\0';
  EXPECT_EQ(kMaxSlotName, deriveSlotName(wide, out, sizeof(out)));
  EXPECT_STREQ("MyLongTuneNa", out) << "a long name is truncated to what a row shows";
}

// The boundary either side of it: a name of exactly kMaxSlotName, and one character more.
TEST(SlotName, HandlesTheRowWidthBoundary) {
  char out[32];
  EXPECT_EQ(12u, deriveSlotName("TwelveChars!:d=4:8c", out, sizeof(out)));
  EXPECT_STREQ("TwelveChars!", out);
  EXPECT_EQ(12u, deriveSlotName("ThirteenChar!:d=4:8c", out, sizeof(out)));
  EXPECT_STREQ("ThirteenChar", out) << "thirteen is truncated, not refused";
}

// #1549: the builtin table cannot play a slot, and the whole range says so.
//
// test_tunes already pins tuneRtttl(kBuiltinCount) and two arbitrary high values as
// nullptr. This is the same contract over every id a slot can have, because a build that
// used this lookup for events is exactly what made a custom tune chosen for DM, Channel
// or Ack audition correctly and then never sound. Anything resolving an id for playback
// has to handle slots itself; the table is not a fallback for them.
TEST(TuneSlotIds, TheBuiltinTableResolvesNoSlotId) {
  for (uint8_t i = 0; i < kSlotCount; i++) {
    EXPECT_EQ(nullptr, tuneRtttl(slotIdAt(i)))
        << "slot " << (int)i << " looks like a builtin -- an event path using tuneRtttl() "
        << "alone would play the wrong tune instead of silently none";
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
