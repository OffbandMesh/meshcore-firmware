// #1865: no channel is written over another. Adding #qcchams and #qcchhv from
// the phone client lost Public on a radio: the client chose slot 0 and the
// firmware wrote it. Pure logic -- no Arduino, no mesh.

#include <gtest/gtest.h>
#include <string.h>
#include "helpers/ChannelSlotGuard.h"

using offband::SlotWrite;
using offband::slotWriteDecision;

namespace {
// Keys as a write carries them (16 bytes): Public's and a hashtag channel's.
const uint8_t kPublic16[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                               0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x1f, 0x1d, 0x1b};
const uint8_t kHashtag16[16] = {0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b,
                                0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f};

// A slot's stored secret: 32 bytes, a 16-byte key zero-extended.
struct Slot {
  uint8_t secret[32];
  explicit Slot(const uint8_t key16[16]) {
    memset(secret, 0, sizeof secret);
    if (key16) memcpy(secret, key16, 16);
  }
};
const Slot kPublic(kPublic16);
const Slot kHashtag(kHashtag16);
const Slot kEmpty(nullptr);
}  // namespace

TEST(ChannelSlotGuard, ADifferentChannelOverAnOccupiedSlotIsRefused) {
  // The bench case: #qcchams into the slot holding Public.
  EXPECT_EQ(SlotWrite::Refuse, slotWriteDecision("Public", kPublic.secret, "#qcchams", kHashtag16));
}

TEST(ChannelSlotGuard, ANewChannelIntoAnEmptySlotIsWritten) {
  EXPECT_EQ(SlotWrite::Write, slotWriteDecision("", kEmpty.secret, "#qcchams", kHashtag16));
  EXPECT_EQ(SlotWrite::Write, slotWriteDecision(nullptr, kEmpty.secret, "#qcchams", kHashtag16));
}

TEST(ChannelSlotGuard, ClearingAnOccupiedSlotIsWritten) {
  // Leave channel, and the first half of a key change: clear, then add.
  EXPECT_EQ(SlotWrite::Write, slotWriteDecision("Public", kPublic.secret, "", kEmpty.secret));
  EXPECT_EQ(SlotWrite::Write, slotWriteDecision("Public", kPublic.secret, nullptr, kEmpty.secret));
}

TEST(ChannelSlotGuard, TheSameChannelAgainIsWritten) {
  EXPECT_EQ(SlotWrite::Write, slotWriteDecision("Public", kPublic.secret, "Public", kPublic16));
}

TEST(ChannelSlotGuard, ARenameWithTheSameKeyIsWritten) {
  EXPECT_EQ(SlotWrite::Write, slotWriteDecision("#test", kHashtag.secret, "Test net", kHashtag16));
}

TEST(ChannelSlotGuard, TheSameNameWithADifferentKeyIsRefused) {
  // A key change in place is still a different channel: clear-then-add.
  EXPECT_EQ(SlotWrite::Refuse, slotWriteDecision("Public", kPublic.secret, "Public", kHashtag16));
}

TEST(ChannelSlotGuard, OneDifferingKeyByteIsEnoughToRefuse) {
  uint8_t almost[16];
  memcpy(almost, kPublic16, 16);
  almost[15] ^= 0x01;
  EXPECT_EQ(SlotWrite::Refuse, slotWriteDecision("Public", kPublic.secret, "Public", almost));
}

TEST(ChannelSlotGuard, A256BitKeyWhoseFirstHalfMatchesIsADifferentChannel) {
  // A 16-byte write would zero bytes 16..31: a different channel over this one.
  Slot wide(kPublic16);
  wide.secret[31] = 0x5a;
  EXPECT_EQ(SlotWrite::Refuse, slotWriteDecision("Wide", wide.secret, "Wide", kPublic16));
}

TEST(ChannelSlotGuard, AMissingKeyFailsClosed) {
  EXPECT_EQ(SlotWrite::Refuse, slotWriteDecision("Public", nullptr, "Public", kPublic16));
  EXPECT_EQ(SlotWrite::Refuse, slotWriteDecision("Public", kPublic.secret, "Public", nullptr));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
