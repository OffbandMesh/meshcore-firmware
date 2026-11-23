#pragma once

#include <stdint.h>

// Offband #2242: who is asking for a contact slot, and what that permits.
//
// The contact table has one allocator (BaseChatMesh::allocateContactSlot) reached
// from two unrelated places: automatic adoption of an advert the radio happened to
// hear, and an add the operator explicitly asked for. Before this header existed
// the allocator could not tell them apart, so AUTO_ADD_OVERWRITE_OLDEST -- a bit in
// the AUTO-ADD config byte, whose job is to restrain automatic adoption -- also
// decided whether the operator was allowed to add a contact at all. With the bit
// off (the setting that protects the table from automatic churn) a full radio
// refused every manual add with ERR_CODE_TABLE_FULL, and every DM to that contact
// then failed with ERR_CODE_NOT_FOUND.
//
// The rules live here, free of Arduino and of mesh types, so they are host-tested
// in test/test_contact_slot_policy rather than only reasoned about.

namespace offband {
namespace contacts {

/// Who asked for the slot.
enum class AddIntent : uint8_t {
  /// The radio decided by itself -- an advert it heard, a contact re-read from
  /// storage at boot, an anonymous request. Governed by the auto-add policy.
  Automatic = 0,
  /// The operator asked for this specific contact: a manual public-key entry, a
  /// pick from discovered contacts, an imported contact card. Not governed by the
  /// auto-add policy, because the operator is not a policy.
  Operator = 1,
};

/// Bit 0 of ContactInfo::flags marks a favourite.
static const uint8_t kFavouriteFlag = 0x01;

/// Favourites are absolute: no path, manual or automatic, may evict one.
inline bool isFavourite(uint8_t flags) { return (flags & kFavouriteFlag) != 0; }

/// May a full table give up a slot to make room for this add?
///
/// An operator-initiated add always may. An automatic one may only when the
/// operator turned the auto-add overwrite policy on.
inline bool mayEvictWhenFull(AddIntent intent, bool auto_overwrite_enabled) {
  return intent == AddIntent::Operator || auto_overwrite_enabled;
}

/// What the victim scan needs to know about one slot.
struct SlotView {
  uint8_t flags;
  uint32_t lastmod;
};

/// Is the table's own bookkeeping within its declared bounds?
///
/// `num_contacts` indexes the array, so a value outside its range must not be
/// used to walk it. Refuse rather than guess (#2242 can't-tell branch).
inline bool rangeIsSane(int num_contacts, int anon_slots, int capacity) {
  return anon_slots >= 0 && capacity >= anon_slots && num_contacts >= anon_slots &&
         num_contacts <= capacity;
}

/// Index of the contact to evict in [begin, end), or -1 when there is none.
///
/// Picks the OLDEST non-favourite by `lastmod`, not the first non-favourite
/// found, and never a favourite. `read(i)` returns that slot's SlotView; it is a
/// callable so this stays a pure function over scalars while the caller keeps its
/// own storage.
template <typename ReadSlot>
int selectEvictableSlot(int begin, int end, ReadSlot read) {
  int oldest_idx = -1;
  uint32_t oldest_lastmod = 0;
  for (int i = begin; i < end; i++) {
    const SlotView slot = read(i);
    // A record whose flags cannot be trusted reads as a favourite via
    // isFavourite(); preservation is the fail-closed direction here.
    if (isFavourite(slot.flags)) continue;
    // Strict '<' keeps the FIRST of several equally-old slots, which is the
    // order the table was already overwritten in.
    if (oldest_idx < 0 || slot.lastmod < oldest_lastmod) {
      oldest_lastmod = slot.lastmod;
      oldest_idx = i;
    }
  }
  return oldest_idx;
}

/// What the table should do with a request for a slot.
enum class SlotOutcome : uint8_t {
  /// Take the next unused slot. `index` is it; the caller grows the table.
  Append = 0,
  /// The table is full and this slot may be reused. `index` is the victim; the
  /// caller is responsible for telling the client what it lost.
  Evict = 1,
  /// No slot. `index` is -1.
  Refuse = 2,
};

struct SlotDecision {
  SlotOutcome outcome;
  int index;
};

/// The whole decision, in one pure function.
///
/// `read(i)` returns slot i's SlotView. The caller does the plumbing -- growing the
/// table, notifying, handing back a pointer -- and none of the deciding, so the
/// rules are testable without a radio.
template <typename ReadSlot>
SlotDecision chooseContactSlot(AddIntent intent, bool auto_overwrite_enabled, int num_contacts,
                               int anon_slots, int capacity, ReadSlot read) {
  SlotDecision d;
  d.outcome = SlotOutcome::Refuse;
  d.index = -1;

  if (!rangeIsSane(num_contacts, anon_slots, capacity)) return d;

  if (num_contacts < capacity) {
    d.outcome = SlotOutcome::Append;
    d.index = num_contacts;
    return d;
  }

  if (!mayEvictWhenFull(intent, auto_overwrite_enabled)) return d;

  const int victim = selectEvictableSlot(anon_slots, num_contacts, read);
  if (victim < 0) return d;   // every slot above the anon pool is a favourite

  d.outcome = SlotOutcome::Evict;
  d.index = victim;
  return d;
}

}  // namespace contacts
}  // namespace offband
