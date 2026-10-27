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

enum class SlotWrite : uint8_t {
  Write,    // empty slot, the same channel again, or a clear
  Refuse,   // a different channel over an occupied slot
};

// slot_name / slot_secret (32 bytes): what the slot holds now. An empty name is
// an empty slot, as MeshCore's own channel tables treat it. new_name /
// new_secret (16 bytes): the write; an empty name clears the slot. The same
// channel means the same 32 stored bytes once the write is zero-extended, so a
// 256-bit key whose first half matches is still a different channel.
inline SlotWrite slotWriteDecision(const char* slot_name, const uint8_t* slot_secret,
                                   const char* new_name, const uint8_t* new_secret) {
  const bool slot_empty = !slot_name || slot_name[0] == 0;
  const bool clearing = !new_name || new_name[0] == 0;
  if (slot_empty || clearing) return SlotWrite::Write;
  if (!slot_secret || !new_secret) return SlotWrite::Refuse;
  if (memcmp(slot_secret, new_secret, kWriteSecretLen) != 0) return SlotWrite::Refuse;
  for (size_t i = kWriteSecretLen; i < kSlotSecretLen; i++) {
    if (slot_secret[i] != 0) return SlotWrite::Refuse;
  }
  return SlotWrite::Write;
}

}  // namespace offband
