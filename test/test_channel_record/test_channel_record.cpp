// Offband #2026: an erased /channels2 record is not a channel.
//
// A real device came up with 38 of 40 slots holding 0xFF in every byte. The firmware
// loaded them as channels, healed slot 0, and then SAVED the whole table back -- writing
// the garbage into the store it had just read, and recreating it on every boot after.
// A transient flash fault became permanent, self-sustaining data loss.
//
// These tests pin the rule that stops that. The weight is on the SAVE side, because the
// load side merely produced phantom channels; the save side is what destroyed the file.

#include <gtest/gtest.h>

#include <helpers/ChannelRecord.h>

#include <cstring>
#include <vector>

using namespace offband;

namespace {

std::vector<uint8_t> nameOf(const char* s) {
  std::vector<uint8_t> v(kChannelNameBytes, 0);
  if (s) std::memcpy(v.data(), s, std::strlen(s));
  return v;
}

std::vector<uint8_t> erasedName() {
  return std::vector<uint8_t>(kChannelNameBytes, 0xFF);
}

}  // namespace

TEST(ChannelRecord, ErasedFlashIsNotAChannel) {
  const auto n = erasedName();
  EXPECT_TRUE(channelRecordErased(n.data()));
  EXPECT_FALSE(channelRecordLoadable(n.data()));
  EXPECT_FALSE(channelRecordSavable(n.data()))
      << "saving an erased record is what made the loss permanent";
}

TEST(ChannelRecord, ARealNameIsAChannel) {
  const auto n = nameOf("#queencitycon");
  EXPECT_FALSE(channelRecordErased(n.data()));
  EXPECT_TRUE(channelRecordLoadable(n.data()));
  EXPECT_TRUE(channelRecordSavable(n.data()));
}

TEST(ChannelRecord, AnEmptySlotIsNeitherErasedNorLoadable) {
  // Deliberately written "no channel here". Distinct from erased: it is not damage, and
  // it MUST stay savable, because that is how a cleared slot keeps its place in a
  // positional file.
  const auto n = nameOf("");
  EXPECT_FALSE(channelRecordErased(n.data()));
  EXPECT_TRUE(channelRecordEmpty(n.data()));
  EXPECT_FALSE(channelRecordLoadable(n.data()));
  EXPECT_TRUE(channelRecordSavable(n.data()));
}

TEST(ChannelRecord, APartiallyErasedRecordIsStillErased) {
  // THE case a "whole record is 0xFF" rule would miss. Power lost mid-save leaves a 0xFF
  // head and a tail of real bytes. That is not a channel either, and it is exactly the
  // record most worth refusing -- a plausible-looking tail is what makes it dangerous.
  auto n = erasedName();
  std::memcpy(n.data() + 8, "ublic", 5);
  EXPECT_TRUE(channelRecordErased(n.data()));
  EXPECT_FALSE(channelRecordSavable(n.data()));
}

TEST(ChannelRecord, AnameMayCONTAIN0xFFWithoutBeingErased) {
  // Only the FIRST byte decides. A name whose later bytes happen to be 0xFF -- stale
  // buffer tail past the terminator, which this firmware really does leak -- is still a
  // channel, and refusing it would delete the user's data to avoid a cosmetic problem.
  auto n = nameOf("#test");
  n[20] = 0xFF;
  n[21] = 0xFF;
  EXPECT_FALSE(channelRecordErased(n.data()));
  EXPECT_TRUE(channelRecordLoadable(n.data()));
  EXPECT_TRUE(channelRecordSavable(n.data()));
}

TEST(ChannelRecord, NullIsRefusedRatherThanDereferenced) {
  EXPECT_FALSE(channelRecordErased(nullptr));
  EXPECT_FALSE(channelRecordEmpty(nullptr));
  EXPECT_FALSE(channelRecordLoadable(nullptr));
  EXPECT_FALSE(channelRecordSavable(nullptr));
}

TEST(ChannelRecord, TheThreeStatesAreMutuallyExclusiveAndExhaustive) {
  // Every possible lead byte lands in exactly one state, so no record can be both
  // "erased" and "a channel", and none can be neither.
  for (int b = 0; b <= 0xFF; b++) {
    std::vector<uint8_t> n(kChannelNameBytes, 0x41);
    n[0] = (uint8_t)b;
    const bool erased = channelRecordErased(n.data());
    const bool empty = channelRecordEmpty(n.data());
    const bool loadable = channelRecordLoadable(n.data());
    EXPECT_EQ(1, (int)erased + (int)empty + (int)loadable)
        << "lead byte " << b << " is in " << ((int)erased + (int)empty + (int)loadable)
        << " states";
    // And the save rule is exactly "not erased".
    EXPECT_EQ(!erased, channelRecordSavable(n.data())) << "lead byte " << b;
  }
}

TEST(ChannelRecord, TheRecordFieldWidthsMatchTheStore) {
  // DataStore writes 4 + 32 + 32 and the stride is 68. If these drift, the rule above is
  // being applied to the wrong bytes.
  EXPECT_EQ(32u, kChannelNameBytes);
  EXPECT_EQ(32u, kChannelSecretBytes);
  EXPECT_EQ(68u, 4 + kChannelNameBytes + kChannelSecretBytes);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
