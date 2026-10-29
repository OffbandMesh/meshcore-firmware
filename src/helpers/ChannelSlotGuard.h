#pragma once

// #1865: whether a channel write may land in a slot. Nobody knows which
// channel sits in which slot -- the client picks one, channels come and go --
// so a write that would put a different channel over one already there is
// refused rather than silently destroying it. Changing a channel's key is
// clear-then-add (owner ruling, Epic #1864).
//
// Pure and Arduino-free: tested natively in test/test_channel_slot_guard.

#include <stdint.h>
#include <string.h>

namespace offband {

// A slot's secret is 32 bytes (mesh::GroupChannel, PUB_KEY_SIZE); a channel
// write carries 16, zero-extended when stored. Channels are matched on all 32.
constexpr size_t kSlotSecretLen = 32;
constexpr size_t kWriteSecretLen = 16;

// Public's slot (owner, 2026-10-28, #1896). A fresh radio seeds Public into an
// empty table, so it lands here, and the apps put it here too.
constexpr uint8_t kPublicSlot = 0;

enum class SlotWrite : uint8_t {
  Write,             // empty slot, the same channel again, a clear, or Public into its slot
  Refuse,            // a different channel over an occupied slot
  RefusePublic,      // a clear of the slot that holds Public (#1896)
  RefusePublicSlot,  // Public into any slot but kPublicSlot (#1896)
};

// Whether a stored 32-byte secret is exactly this 16-byte key, zero-extended.
inline bool slotHoldsKey(const uint8_t* slot_secret, const uint8_t* key) {
  if (!slot_secret || !key) return false;
  if (memcmp(slot_secret, key, kWriteSecretLen) != 0) return false;
  for (size_t i = kWriteSecretLen; i < kSlotSecretLen; i++) {
    if (slot_secret[i] != 0) return false;
  }
  return true;
}

// slot_name / slot_secret (32 bytes): what the slot holds now. An empty name is
// an empty slot, as MeshCore's own channel tables treat it. new_name /
// new_secret (16 bytes): the write; an empty name clears the slot. The same
// channel means the same 32 stored bytes once the write is zero-extended, so a
// 256-bit key whose first half matches is still a different channel.
//
// public_secret (16 bytes, kPublicChannelKey) and slot (the index written), for
// the owner's rules on Public (2026-10-28, #1896):
// - Public can't be deleted or left: a clear of the slot holding it is refused,
//   whichever client asks. A rename keeps the key, so it is still allowed.
// - Public lives in kPublicSlot. Written there, it displaces whatever channel
//   the slot held ("wipe out what I have, I can re-add it"). Written anywhere
//   else it is refused, unless that slot already holds it (an older radio's
//   Public, renamed in place).
inline SlotWrite slotWriteDecision(uint8_t slot, const char* slot_name, const uint8_t* slot_secret,
                                   const char* new_name, const uint8_t* new_secret,
                                   const uint8_t* public_secret) {
  const bool slot_empty = !slot_name || slot_name[0] == 0;
  const bool clearing = !new_name || new_name[0] == 0;
  if (clearing) {
    return !slot_empty && slotHoldsKey(slot_secret, public_secret) ? SlotWrite::RefusePublic : SlotWrite::Write;
  }
  const bool writing_public =
      public_secret && new_secret && memcmp(new_secret, public_secret, kWriteSecretLen) == 0;
  if (writing_public) {
    if (slot == kPublicSlot) return SlotWrite::Write;
    return !slot_empty && slotHoldsKey(slot_secret, public_secret) ? SlotWrite::Write : SlotWrite::RefusePublicSlot;
  }
  if (slot_empty) return SlotWrite::Write;
  return slotHoldsKey(slot_secret, new_secret) ? SlotWrite::Write : SlotWrite::Refuse;
}

// #1896: where boot puts Public back when no slot holds it -- a radio that lost
// it under older firmware. Slot 0 if it is empty, else the first empty slot:
// boot never displaces a channel, because a private key there could not be
// recovered (displacing is for a join the user asked for). -1 when Public is
// already held, or no slot is empty. get(i, &name, &secret) reads slot i.
template <class Get>
int publicRepairSlot(int count, Get get, const uint8_t* public_secret) {
  int first_free = -1;
  bool public_slot_free = false;
  for (int i = 0; i < count; i++) {
    const char* name = nullptr;
    const uint8_t* secret = nullptr;
    if (!get(i, &name, &secret)) continue;
    const bool empty = !name || name[0] == 0;
    if (!empty && slotHoldsKey(secret, public_secret)) return -1;
    if (!empty) continue;
    if (i == kPublicSlot) public_slot_free = true;
    if (first_free < 0) first_free = i;
  }
  return public_slot_free ? kPublicSlot : first_free;
}

}  // namespace offband
