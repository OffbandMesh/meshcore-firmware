// Offband #2242: the contact table's slot rules.
//
// What the device could not show: AUTO_ADD_OVERWRITE_OLDEST is a bit in the
// AUTO-ADD config byte, and it was deciding whether the OPERATOR was allowed to add
// a contact. With the bit off -- the setting that stops the radio churning the table
// on its own -- a full radio refused every manual add with ERR_CODE_TABLE_FULL, and
// every DM to that contact then came back ERR_CODE_NOT_FOUND. These tests pin both
// halves: the operator is not governed by auto-add policy, and a favourite is never
// the price of making room.

#include <gtest/gtest.h>

#include <vector>

#include "helpers/ContactSlotPolicy.h"

using offband::contacts::AddIntent;
using offband::contacts::chooseContactSlot;
using offband::contacts::isFavourite;
using offband::contacts::kFavouriteFlag;
using offband::contacts::mayEvictWhenFull;
using offband::contacts::rangeIsSane;
using offband::contacts::selectEvictableSlot;
using offband::contacts::SlotDecision;
using offband::contacts::SlotOutcome;
using offband::contacts::SlotView;

namespace {

// The radio's real geometry: 8 anonymous-request slots, then MAX_CONTACTS named
// ones. 350 is what the owner's heltec_v4_tft_companion_radio_ble_gui carries.
const int kAnon = 8;
const int kNamed = 350;
const int kCapacity = kAnon + kNamed;

// A stand-in for contacts[]: only the two fields the decision reads.
class Table {
  std::vector<SlotView> slots_;

public:
  explicit Table(int named_count) {
    slots_.resize(kCapacity);
    for (int i = 0; i < kCapacity; i++) {
      slots_[i].flags = 0;
      // Distinct, increasing ages so "the oldest" is unambiguous. Slot kAnon is
      // the oldest named contact.
      slots_[i].lastmod = 1000 + (uint32_t)i;
    }
    num_contacts = kAnon + named_count;
  }

  int num_contacts;

  void makeFavourite(int idx) { slots_[idx].flags |= kFavouriteFlag; }
  void setLastmod(int idx, uint32_t t) { slots_[idx].lastmod = t; }
  void favouriteAllNamed() {
    for (int i = kAnon; i < num_contacts; i++) makeFavourite(i);
  }

  // The accessor chooseContactSlot() reads through.
  SlotView operator()(int i) const { return slots_[i]; }

  SlotDecision decide(AddIntent intent, bool auto_overwrite) const {
    return chooseContactSlot(intent, auto_overwrite, num_contacts, kAnon, kCapacity, *this);
  }
};

Table fullTable() { return Table(kNamed); }

}  // namespace

// ─────────────────────────────── the defect itself ───────────────────────────────

// The case the owner hit: table at 350 of 350, overwrite OFF, adding a contact by
// hand. Before #2242 this refused and the DM that followed failed NOT_FOUND.
TEST(ManualAdd, FullTableWithOverwriteOff_EvictsOldestNonFavourite) {
  const Table t = fullTable();
  const SlotDecision d = t.decide(AddIntent::Operator, /*auto_overwrite=*/false);
  EXPECT_EQ(SlotOutcome::Evict, d.outcome);
  EXPECT_EQ(kAnon, d.index);  // the oldest named slot
}

// The auto-add bit is simply not part of this decision, so turning it on changes
// nothing for a manual add.
TEST(ManualAdd, OverwriteBitIsIrrelevant) {
  const Table t = fullTable();
  const SlotDecision off = t.decide(AddIntent::Operator, false);
  const SlotDecision on = t.decide(AddIntent::Operator, true);
  EXPECT_EQ(off.outcome, on.outcome);
  EXPECT_EQ(off.index, on.index);
}

// Favourites are absolute. A table with nothing else to give refuses -- it does not
// buy the add with a favourite.
TEST(ManualAdd, EveryNamedSlotAFavourite_Refuses) {
  Table t = fullTable();
  t.favouriteAllNamed();
  const SlotDecision d = t.decide(AddIntent::Operator, false);
  EXPECT_EQ(SlotOutcome::Refuse, d.outcome);
  EXPECT_EQ(-1, d.index);
}

// ──────────────────────── automatic adoption is unchanged ───────────────────────

TEST(AutomaticAdd, FullTableWithOverwriteOff_RefusesExactlyAsBefore) {
  const Table t = fullTable();
  const SlotDecision d = t.decide(AddIntent::Automatic, /*auto_overwrite=*/false);
  EXPECT_EQ(SlotOutcome::Refuse, d.outcome);
  EXPECT_EQ(-1, d.index);
}

TEST(AutomaticAdd, FullTableWithOverwriteOn_EvictsOldestNonFavourite) {
  const Table t = fullTable();
  const SlotDecision d = t.decide(AddIntent::Automatic, /*auto_overwrite=*/true);
  EXPECT_EQ(SlotOutcome::Evict, d.outcome);
  EXPECT_EQ(kAnon, d.index);
}

TEST(AutomaticAdd, OverwriteOnStillWillNotTakeAFavourite) {
  Table t = fullTable();
  t.favouriteAllNamed();
  const SlotDecision d = t.decide(AddIntent::Automatic, true);
  EXPECT_EQ(SlotOutcome::Refuse, d.outcome);
}

// ─────────────────────────────── choosing the victim ────────────────────────────

// The oldest by lastmod, not the first non-favourite the scan happens to reach.
TEST(Victim, IsTheOldestByLastmodNotTheFirstFound) {
  Table t = fullTable();
  // Slot kAnon is oldest by construction; make a later slot older still.
  const int oldest = kAnon + 200;
  t.setLastmod(oldest, 1);
  for (AddIntent intent : {AddIntent::Operator, AddIntent::Automatic}) {
    const SlotDecision d = t.decide(intent, true);
    EXPECT_EQ(SlotOutcome::Evict, d.outcome);
    EXPECT_EQ(oldest, d.index);
  }
}

// A favourite that is the oldest contact on the radio is skipped, and the next
// oldest non-favourite is taken instead.
TEST(Victim, SkipsAFavouriteEvenWhenItIsTheOldest) {
  Table t = fullTable();
  t.makeFavourite(kAnon);  // the oldest
  const SlotDecision d = t.decide(AddIntent::Operator, false);
  EXPECT_EQ(SlotOutcome::Evict, d.outcome);
  EXPECT_EQ(kAnon + 1, d.index);
}

// A whole run of favourites at the oldest end is skipped, not just one.
TEST(Victim, SkipsEveryFavouriteInARun) {
  Table t = fullTable();
  for (int i = kAnon; i < kAnon + 40; i++) t.makeFavourite(i);
  const SlotDecision d = t.decide(AddIntent::Operator, false);
  EXPECT_EQ(SlotOutcome::Evict, d.outcome);
  EXPECT_EQ(kAnon + 40, d.index);
}

// The 8 anonymous-request slots are a separate pool with its own lifetime; a named
// add must never reclaim one, however old it looks.
TEST(Victim, NeverTakesAnAnonymousRequestSlot) {
  Table t = fullTable();
  for (int i = 0; i < kAnon; i++) t.setLastmod(i, 0);  // older than everything
  const SlotDecision d = t.decide(AddIntent::Operator, false);
  EXPECT_EQ(SlotOutcome::Evict, d.outcome);
  EXPECT_GE(d.index, kAnon);
}

// Two slots equally old: keep the earlier index, which is the order the table was
// already being overwritten in before #2242.
TEST(Victim, EquallyOldSlotsKeepTheFirst) {
  Table t = fullTable();
  t.setLastmod(kAnon + 5, 7);
  t.setLastmod(kAnon + 9, 7);
  const SlotDecision d = t.decide(AddIntent::Operator, false);
  EXPECT_EQ(kAnon + 5, d.index);
}

// A table whose every contact carries the maximum timestamp still yields a victim.
// The old scan seeded its "oldest so far" with 0xFFFFFFFF and required a strictly
// smaller value, so this table had nothing to give and a manual add would have been
// refused with slots sitting free of favourites.
TEST(Victim, FoundEvenWhenEveryLastmodIsUINT32_MAX) {
  Table t = fullTable();
  for (int i = kAnon; i < kCapacity; i++) t.setLastmod(i, 0xFFFFFFFFu);
  const SlotDecision d = t.decide(AddIntent::Operator, false);
  EXPECT_EQ(SlotOutcome::Evict, d.outcome);
  EXPECT_EQ(kAnon, d.index);
}

// ──────────────────────────── a table that is not full ──────────────────────────

TEST(NotFull, AppendsAtTheEndForEitherIntent) {
  const Table t(10);
  for (AddIntent intent : {AddIntent::Operator, AddIntent::Automatic}) {
    for (bool overwrite : {false, true}) {
      const SlotDecision d = t.decide(intent, overwrite);
      EXPECT_EQ(SlotOutcome::Append, d.outcome);
      EXPECT_EQ(kAnon + 10, d.index);
    }
  }
}

TEST(NotFull, TheLastFreeSlotIsStillAnAppend) {
  const Table t(kNamed - 1);
  const SlotDecision d = t.decide(AddIntent::Automatic, false);
  EXPECT_EQ(SlotOutcome::Append, d.outcome);
  EXPECT_EQ(kCapacity - 1, d.index);
}

TEST(NotFull, AnEmptyTableAppendsAboveTheAnonymousPool) {
  const Table t(0);
  const SlotDecision d = t.decide(AddIntent::Operator, false);
  EXPECT_EQ(SlotOutcome::Append, d.outcome);
  EXPECT_EQ(kAnon, d.index);
}

// ───────────────────────── bookkeeping that cannot be trusted ───────────────────

// num_contacts indexes the array, so a value outside its bounds is refused rather
// than walked. No fail-open branch exists here by design.
TEST(Bounds, OutOfRangeCountIsRefusedNotIndexed) {
  Table t = fullTable();

  t.num_contacts = kCapacity + 1;
  EXPECT_EQ(SlotOutcome::Refuse, t.decide(AddIntent::Operator, true).outcome);

  t.num_contacts = kAnon - 1;
  EXPECT_EQ(SlotOutcome::Refuse, t.decide(AddIntent::Operator, true).outcome);

  t.num_contacts = -5;
  EXPECT_EQ(SlotOutcome::Refuse, t.decide(AddIntent::Operator, true).outcome);
}

TEST(Bounds, RangeIsSaneAcceptsTheRealGeometry) {
  EXPECT_TRUE(rangeIsSane(kAnon, kAnon, kCapacity));       // empty
  EXPECT_TRUE(rangeIsSane(kCapacity, kAnon, kCapacity));   // full
  EXPECT_FALSE(rangeIsSane(kCapacity + 1, kAnon, kCapacity));
  EXPECT_FALSE(rangeIsSane(kAnon - 1, kAnon, kCapacity));
}

// ─────────────────────────── other build geometries ────────────────────────────
//
// MAX_CONTACTS and MAX_ANON_CONTACTS are both per-env overrides: 350/8 on the Heltec
// V4 TFT companion, 5/3 on the observer builds (#701). The decision must hold at any
// size, including an anonymous pool of zero, so these walk the geometry directly
// rather than through the 350/8 fixture above.

namespace {

// The same decision over a flat vector, so a test can pick any geometry.
SlotDecision decideWith(AddIntent intent, bool auto_overwrite, const std::vector<SlotView>& slots,
                        int num_contacts, int anon, int capacity) {
  return chooseContactSlot(intent, auto_overwrite, num_contacts, anon, capacity,
                           [&slots](int i) { return slots[(size_t)i]; });
}

std::vector<SlotView> plainSlots(int n) {
  std::vector<SlotView> s((size_t)n);
  for (int i = 0; i < n; i++) {
    s[(size_t)i].flags = 0;
    s[(size_t)i].lastmod = 1000 + (uint32_t)i;
  }
  return s;
}

}  // namespace

// The observer builds: MAX_CONTACTS=5, MAX_ANON_CONTACTS=3.
TEST(Geometry, ObserverSizedTableEvictsForTheOperator) {
  const std::vector<SlotView> s = plainSlots(8);
  const SlotDecision d = decideWith(AddIntent::Operator, false, s, 8, 3, 8);
  EXPECT_EQ(SlotOutcome::Evict, d.outcome);
  EXPECT_EQ(3, d.index);   // the first named slot, which is the oldest
}

TEST(Geometry, ObserverSizedTableStillRefusesAnAutomaticAddWithOverwriteOff) {
  const std::vector<SlotView> s = plainSlots(8);
  EXPECT_EQ(SlotOutcome::Refuse, decideWith(AddIntent::Automatic, false, s, 8, 3, 8).outcome);
}

// MAX_ANON_CONTACTS=0: no anonymous pool at all, so slot 0 is a named contact and is
// a legitimate victim. Nothing may index below zero.
TEST(Geometry, NoAnonymousPoolAtAll) {
  const std::vector<SlotView> s = plainSlots(4);
  EXPECT_EQ(SlotOutcome::Append, decideWith(AddIntent::Operator, false, s, 0, 0, 4).outcome);
  EXPECT_EQ(0, decideWith(AddIntent::Operator, false, s, 0, 0, 4).index);

  const SlotDecision full = decideWith(AddIntent::Operator, false, s, 4, 0, 4);
  EXPECT_EQ(SlotOutcome::Evict, full.outcome);
  EXPECT_EQ(0, full.index);
}

// A table that is nothing but anonymous slots has no named contact to give up.
TEST(Geometry, TableThatIsAllAnonymousPoolRefuses) {
  const std::vector<SlotView> s = plainSlots(8);
  EXPECT_EQ(SlotOutcome::Refuse, decideWith(AddIntent::Operator, true, s, 8, 8, 8).outcome);
}

// A one-slot table: the single named contact is evictable for the operator, and is
// protected the moment it is favourited.
TEST(Geometry, SingleNamedSlot) {
  std::vector<SlotView> s = plainSlots(1);
  EXPECT_EQ(SlotOutcome::Evict, decideWith(AddIntent::Operator, false, s, 1, 0, 1).outcome);
  s[0].flags = kFavouriteFlag;
  EXPECT_EQ(SlotOutcome::Refuse, decideWith(AddIntent::Operator, false, s, 1, 0, 1).outcome);
}

TEST(Geometry, CapacityBelowTheAnonymousPoolIsRefusedNotIndexed) {
  const std::vector<SlotView> s = plainSlots(4);
  // capacity < anon_slots is not a table this code can reason about.
  EXPECT_EQ(SlotOutcome::Refuse, decideWith(AddIntent::Operator, true, s, 2, 4, 2).outcome);
  EXPECT_FALSE(rangeIsSane(2, 4, 2));
  EXPECT_FALSE(rangeIsSane(0, -1, 4));
}

// ────────────────────────────── the rules in isolation ──────────────────────────

TEST(Policy, OperatorMayEvictRegardlessOfTheAutoAddBit) {
  EXPECT_TRUE(mayEvictWhenFull(AddIntent::Operator, false));
  EXPECT_TRUE(mayEvictWhenFull(AddIntent::Operator, true));
}

TEST(Policy, AutomaticMayEvictOnlyWhenTheOperatorAllowedIt) {
  EXPECT_FALSE(mayEvictWhenFull(AddIntent::Automatic, false));
  EXPECT_TRUE(mayEvictWhenFull(AddIntent::Automatic, true));
}

// The enumerator values are what makes the DEFAULT argument on
// allocateContactSlot()/addContact() the restrictive one: a caller that says
// nothing is Automatic, and cannot gain eviction power by omission.
TEST(Policy, AutomaticIsTheZeroValue) {
  EXPECT_EQ(0, (int)AddIntent::Automatic);
  EXPECT_EQ(1, (int)AddIntent::Operator);
}

TEST(Policy, FavouriteIsBitZeroOfTheFlagsByte) {
  EXPECT_EQ(0x01, (int)kFavouriteFlag);
  EXPECT_FALSE(isFavourite(0x00));
  EXPECT_TRUE(isFavourite(0x01));
  EXPECT_TRUE(isFavourite(0xFF));   // a record whose flags make no sense is protected
  EXPECT_FALSE(isFavourite(0xFE));  // every other flag set, favourite clear
}

TEST(Policy, AnEmptyScanRangeHasNoVictim) {
  const Table t = fullTable();
  EXPECT_EQ(-1, selectEvictableSlot(kAnon, kAnon, t));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
