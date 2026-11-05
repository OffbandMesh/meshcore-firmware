#pragma once

// Is a stored /channels2 record a channel, or is it erased flash? (Offband #2026)
//
// WHY THIS EXISTS
// ---------------
// A T1000-E came up with 38 of its 40 channel slots holding 0xFF in every byte of the
// name and secret. 0xFF is what erased NOR flash reads as -- it is not a channel, and no
// channel can ever legitimately look like one, because a name is a NUL-terminated string
// and 0xFF is not a valid lead byte for any name a user can type.
//
// The firmware had no opinion about this. loadChannels() installed all 38 as channels,
// placePublicInSlot0() then healed slot 0 and set `changed`, and the resulting
// saveChannels() WROTE THE WHOLE TABLE BACK -- persisting the garbage into the store it
// had just been read from. From then on every boot re-read it and re-saved it. A
// transient flash problem became permanent, self-sustaining data loss with no path to
// recovery, and the owner's real channel list was gone.
//
// What erased the flash in the first place is a separate, still-unidentified question.
// This header does not answer it. It makes the firmware SURVIVE it: an erased record is
// not loaded, and never written back, so the next boot comes up with whatever genuinely
// remains and an import can restore the rest.
//
// Pure and Arduino-free so the whole rule is unit-testable on the host.

#include <stdint.h>
#include <stddef.h>

namespace offband {

// A record's name field is 32 bytes, its secret 32, as written by DataStore.
constexpr size_t kChannelNameBytes = 32;
constexpr size_t kChannelSecretBytes = 32;

// THE TEST IS THE NAME'S FIRST BYTE, and deliberately nothing cleverer.
//
// 0xFF cannot begin a name: names are NUL-terminated text, and a user cannot type 0xFF.
// An empty slot is 0x00. So the three states are distinguishable by one byte, and a rule
// that reads one byte cannot itself be the thing that misjudges a record.
//
// Rejected: "every byte is 0xFF". A partially-written record -- power lost mid-save --
// has a 0xFF head and a tail of real bytes, and is still not a channel. Requiring the
// whole record to be erased would load exactly the corrupt records that matter most.
inline bool channelRecordErased(const uint8_t* name) {
  return name != nullptr && name[0] == 0xFF;
}

// An empty slot: written deliberately, means "no channel here". Distinct from erased,
// which means "this was never written, or was wiped". Both are skipped on load, but only
// erased indicates damage worth reporting.
inline bool channelRecordEmpty(const uint8_t* name) {
  return name != nullptr && name[0] == 0x00;
}

// Worth loading into the channel table?
inline bool channelRecordLoadable(const uint8_t* name) {
  return name != nullptr && !channelRecordErased(name) && !channelRecordEmpty(name);
}

// Worth writing back to the store? An erased record must NEVER be written: that is the
// step that made a transient fault permanent. It is written as an empty slot instead --
// zeros -- so the file keeps its fixed record count and stride while the damage stops
// propagating.
inline bool channelRecordSavable(const uint8_t* name) {
  return name != nullptr && !channelRecordErased(name);
}

}  // namespace offband
