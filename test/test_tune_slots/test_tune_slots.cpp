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
#include <string>

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

// ---- the line format -----------------------------------------------------------------

TEST(SlotLines, PullsTheRequestedLine) {
  const char blob[] = "one\ntwo\nthree";
  char out[16];
  EXPECT_EQ(3u, copyLineAt(blob, sizeof(blob) - 1, 0, out, sizeof(out)));
  EXPECT_STREQ("one", out);
  EXPECT_EQ(3u, copyLineAt(blob, sizeof(blob) - 1, 1, out, sizeof(out)));
  EXPECT_STREQ("two", out);
  EXPECT_EQ(5u, copyLineAt(blob, sizeof(blob) - 1, 2, out, sizeof(out)));
  EXPECT_STREQ("three", out) << "a final line with no newline must still read";
}

// An absent line and a blank line are the same thing -- an empty slot -- so the caller
// needs no third case and a short file is not an error.
TEST(SlotLines, AbsentAndBlankLinesBothReadEmpty) {
  const char blob[] = "one\n\nthree\n";
  char out[16];
  EXPECT_EQ(0u, copyLineAt(blob, sizeof(blob) - 1, 1, out, sizeof(out)));
  EXPECT_STREQ("", out) << "a blank line is an empty slot";
  EXPECT_EQ(0u, copyLineAt(blob, sizeof(blob) - 1, 7, out, sizeof(out)));
  EXPECT_STREQ("", out) << "a file shorter than the slot count is not an error";
}

// A file that has been through a Windows editor must still read.
TEST(SlotLines, DropsACarriageReturn) {
  const char blob[] = "Nokia:d=4:8e6\r\nMario:d=4:8c\r\n";
  char out[32];
  EXPECT_EQ(13u, copyLineAt(blob, sizeof(blob) - 1, 0, out, sizeof(out)));
  EXPECT_STREQ("Nokia:d=4:8e6", out);
  EXPECT_EQ(12u, copyLineAt(blob, sizeof(blob) - 1, 1, out, sizeof(out)));
  EXPECT_STREQ("Mario:d=4:8c", out) << "a CRLF line after the first must also strip \\r";
}

TEST(SlotLines, TruncatesToTheCallersBufferAndAlwaysTerminates) {
  const char blob[] = "abcdefghij\n";
  char out[4];
  EXPECT_EQ(3u, copyLineAt(blob, sizeof(blob) - 1, 0, out, sizeof(out)));
  EXPECT_STREQ("abc", out);
  char none[1] = {'Z'};
  EXPECT_EQ(0u, copyLineAt(blob, sizeof(blob) - 1, 0, none, 0));
  EXPECT_EQ('Z', none[0]);
  EXPECT_EQ(0u, copyLineAt(nullptr, 10, 0, out, sizeof(out)));
  EXPECT_STREQ("", out);
}

// A truncated, empty or garbage file must read as "no slots" rather than as noise. A
// corrupt blob is the realistic failure on a device that lost power mid-write.
TEST(SlotLines, AGarbageOrEmptyBlobYieldsNoSlots) {
  char out[kMaxRtttl + 1];
  EXPECT_EQ(0u, copyLineAt("", 0, 0, out, sizeof(out)));
  EXPECT_STREQ("", out);
  // No newline anywhere, longer than one slot: line 0 reads, every later line is empty.
  std::string junk(500, 'x');
  EXPECT_GT(copyLineAt(junk.data(), junk.size(), 0, out, sizeof(out)), 0u);
  EXPECT_EQ(kMaxRtttl, strlen(out)) << "a single huge line must clamp to the slot size";
  for (size_t i = 1; i < kSlotCount; i++) {
    EXPECT_EQ(0u, copyLineAt(junk.data(), junk.size(), i, out, sizeof(out)))
        << "slot " << i << " of a newline-free blob should be empty";
  }
}

// The whole round trip the badge performs: a file of eight lines in, a name per filled
// slot out, empties staying empty.
TEST(SlotLines, AWholeFileResolvesToNamesAndEmpties) {
  const char blob[] =
      "Nokia:d=4,o=5,b=63:8e6\n"
      "\n"
      "Mario:d=4,o=5,b=200:8e6,8e6\n";
  char line[kMaxRtttl + 1];
  char name[kMaxSlotName + 1];
  const char* want[kSlotCount] = {"Nokia", "", "Mario", "", "", "", "", ""};
  for (uint8_t i = 0; i < kSlotCount; i++) {
    copyLineAt(blob, sizeof(blob) - 1, i, line, sizeof(line));
    deriveSlotName(line, name, sizeof(name));
    EXPECT_STREQ(want[i], name) << "slot " << (int)i;
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
