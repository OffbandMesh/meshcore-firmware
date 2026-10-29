// #1865: no channel is written over another. Adding #qcchams and #qcchhv from
// the phone client lost Public on a radio: the client chose slot 0 and the
// firmware wrote it. #1896: and Public can't be cleared at all (owner,
// 2026-10-28). Pure logic -- no Arduino, no mesh.

#include <gtest/gtest.h>
#include <string.h>
#include "helpers/ChannelSlotGuard.h"

using offband::SlotWrite;
using offband::slotWriteDecision;

namespace {
// Keys as a write carries them (16 bytes): Public's (the real, published key,
// as helpers/ChannelKeys.cpp holds it) and a hashtag channel's.
const uint8_t kPublic16[16] = {0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a,
                               0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72};
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

// Every call passes Public's key, as cmdSetChannel does. decide() writes a slot
// that isn't Public's; decideAt() names the slot.
SlotWrite decideAt(uint8_t slot, const char* slot_name, const uint8_t* slot_secret, const char* new_name,
                   const uint8_t* new_secret) {
  return slotWriteDecision(slot, slot_name, slot_secret, new_name, new_secret, kPublic16);
}
SlotWrite decide(const char* slot_name, const uint8_t* slot_secret, const char* new_name,
                 const uint8_t* new_secret) {
  return decideAt(3, slot_name, slot_secret, new_name, new_secret);
}
}  // namespace

static_assert(offband::kPublicSlot == 0, "Public lives in slot 0 (owner, 2026-10-28)");

TEST(ChannelSlotGuard, ADifferentChannelOverAnOccupiedSlotIsRefused) {
  // The bench case: #qcchams into the slot holding Public.
  EXPECT_EQ(SlotWrite::Refuse, decide("Public", kPublic.secret, "#qcchams", kHashtag16));
}

TEST(ChannelSlotGuard, ANewChannelIntoAnEmptySlotIsWritten) {
  EXPECT_EQ(SlotWrite::Write, decide("", kEmpty.secret, "#qcchams", kHashtag16));
  EXPECT_EQ(SlotWrite::Write, decide(nullptr, kEmpty.secret, "#qcchams", kHashtag16));
}

TEST(ChannelSlotGuard, PublicIntoItsEmptySlotIsWritten) {
  EXPECT_EQ(SlotWrite::Write, decideAt(0, "", kEmpty.secret, "Public", kPublic16));
}

TEST(ChannelSlotGuard, PublicIntoItsSlotDisplacesAnotherChannel) {
  // "wipe out what I have, I can re-add it" (owner, 2026-10-28).
  EXPECT_EQ(SlotWrite::Write, decideAt(0, "#test", kHashtag.secret, "Public", kPublic16));
}

TEST(ChannelSlotGuard, PublicIntoAnyOtherSlotIsRefused) {
  EXPECT_EQ(SlotWrite::RefusePublicSlot, decideAt(3, "", kEmpty.secret, "Public", kPublic16));
  EXPECT_EQ(SlotWrite::RefusePublicSlot, decideAt(7, "#test", kHashtag.secret, "Public", kPublic16));
}

TEST(ChannelSlotGuard, AnOlderRadiosPublicElsewhereCanStillBeRenamed) {
  EXPECT_EQ(SlotWrite::Write, decideAt(5, "Public", kPublic.secret, "Everyone", kPublic16));
}

TEST(ChannelSlotGuard, AnotherChannelOverPublicsSlotIsRefused) {
  EXPECT_EQ(SlotWrite::Refuse, decideAt(0, "Public", kPublic.secret, "#qcchams", kHashtag16));
}

TEST(ChannelSlotGuard, AnotherChannelIntoPublicsEmptySlotIsWritten) {
  // Slot 0 isn't reserved while empty; Public displaces it when joined.
  EXPECT_EQ(SlotWrite::Write, decideAt(0, "", kEmpty.secret, "#qcchams", kHashtag16));
}

TEST(ChannelSlotGuard, ClearingAnyOtherChannelIsWritten) {
  // Leave channel, and the first half of a key change: clear, then add.
  EXPECT_EQ(SlotWrite::Write, decide("#test", kHashtag.secret, "", kEmpty.secret));
  EXPECT_EQ(SlotWrite::Write, decide("#test", kHashtag.secret, nullptr, kEmpty.secret));
}

TEST(ChannelSlotGuard, ClearingPublicIsRefused) {
  // #1896: Public can't be deleted or left, whichever client asks.
  EXPECT_EQ(SlotWrite::RefusePublic, decide("Public", kPublic.secret, "", kEmpty.secret));
  EXPECT_EQ(SlotWrite::RefusePublic, decide("Public", kPublic.secret, nullptr, kEmpty.secret));
}

TEST(ChannelSlotGuard, PublicIsKnownByItsKeyNotItsName) {
  // Renamed, it's still Public; a channel merely named "Public" isn't.
  EXPECT_EQ(SlotWrite::RefusePublic, decide("Everyone", kPublic.secret, "", kEmpty.secret));
  EXPECT_EQ(SlotWrite::Write, decide("Public", kHashtag.secret, "", kEmpty.secret));
}

TEST(ChannelSlotGuard, A256BitKeyWhoseFirstHalfIsPublicsCanBeCleared) {
  Slot wide(kPublic16);
  wide.secret[31] = 0x5a;
  EXPECT_EQ(SlotWrite::Write, decide("Wide", wide.secret, "", kEmpty.secret));
}

TEST(ChannelSlotGuard, RenamingPublicIsWritten) {
  // Same key, so the same channel: a rename never loses Public.
  EXPECT_EQ(SlotWrite::Write, decide("Public", kPublic.secret, "Everyone", kPublic16));
}

TEST(ChannelSlotGuard, TheSameChannelAgainIsWritten) {
  EXPECT_EQ(SlotWrite::Write, decide("Public", kPublic.secret, "Public", kPublic16));
}

TEST(ChannelSlotGuard, ARenameWithTheSameKeyIsWritten) {
  EXPECT_EQ(SlotWrite::Write, decide("#test", kHashtag.secret, "Test net", kHashtag16));
}

TEST(ChannelSlotGuard, TheSameNameWithADifferentKeyIsRefused) {
  // A key change in place is still a different channel: clear-then-add.
  EXPECT_EQ(SlotWrite::Refuse, decide("Public", kPublic.secret, "Public", kHashtag16));
}

TEST(ChannelSlotGuard, OneDifferingKeyByteIsEnoughToRefuse) {
  uint8_t almost[16];
  memcpy(almost, kPublic16, 16);
  almost[15] ^= 0x01;
  EXPECT_EQ(SlotWrite::Refuse, decide("Public", kPublic.secret, "Public", almost));
}

TEST(ChannelSlotGuard, A256BitKeyWhoseFirstHalfMatchesIsADifferentChannel) {
  // A 16-byte write would zero bytes 16..31: a different channel over this one.
  Slot wide(kHashtag16);
  wide.secret[31] = 0x5a;
  EXPECT_EQ(SlotWrite::Refuse, decide("Wide", wide.secret, "Wide", kHashtag16));
}

TEST(ChannelSlotGuard, AMissingKeyFailsClosed) {
  EXPECT_EQ(SlotWrite::Refuse, decide("#test", nullptr, "#test", kHashtag16));
  EXPECT_EQ(SlotWrite::Refuse, decide("#test", kHashtag.secret, "#test", nullptr));
}

// #1896: boot puts Public back on a radio that lost it under older firmware.
namespace {
struct Table {
  const char* names[5];
  const uint8_t* secrets[5];
  int repair() const {
    return offband::publicRepairSlot(
        5,
        [this](int i, const char** n, const uint8_t** s) {
          *n = names[i];
          *s = secrets[i];
          return true;
        },
        kPublic16);
  }
};
}  // namespace

TEST(PublicRepair, PublicHeldAnywhereNeedsNothing) {
  Table t{{"#a", "", "Public", "", ""}, {kHashtag.secret, kEmpty.secret, kPublic.secret, kEmpty.secret, kEmpty.secret}};
  EXPECT_EQ(-1, t.repair());
  Table renamed{{"Everyone", "", "", "", ""}, {kPublic.secret, kEmpty.secret, kEmpty.secret, kEmpty.secret, kEmpty.secret}};
  EXPECT_EQ(-1, renamed.repair());
}

TEST(PublicRepair, LostPublicGoesBackIntoAnEmptySlotZero) {
  Table t{{"", "#a", "", "", ""}, {kEmpty.secret, kHashtag.secret, kEmpty.secret, kEmpty.secret, kEmpty.secret}};
  EXPECT_EQ(0, t.repair());
}

TEST(PublicRepair, BootNeverDisplacesTheChannelInSlotZero) {
  // A private key in slot 0 couldn't be recovered: the first empty slot instead.
  Table t{{"Family", "#a", "", "", ""}, {kHashtag.secret, kHashtag.secret, kEmpty.secret, kEmpty.secret, kEmpty.secret}};
  EXPECT_EQ(2, t.repair());
}

TEST(PublicRepair, AChannelMerelyNamedPublicIsNotPublic) {
  Table t{{"Public", "", "", "", ""}, {kHashtag.secret, kEmpty.secret, kEmpty.secret, kEmpty.secret, kEmpty.secret}};
  EXPECT_EQ(1, t.repair());
}

TEST(PublicRepair, AFullTableWithoutPublicHasNowhereToPutIt) {
  Table t{{"#a", "#b", "#c", "#d", "#e"}, {kHashtag.secret, kHashtag.secret, kHashtag.secret, kHashtag.secret, kHashtag.secret}};
  EXPECT_EQ(-1, t.repair());
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
