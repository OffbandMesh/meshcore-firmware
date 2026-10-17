#pragma once

// #1474: eight custom tune slots -- the format, the id space and the name rule.
//
// Pure and free of the filesystem so it is unit-tested natively; the badge's store does
// the file I/O and the staging buffer. #1478 reuses the same line and name rules for a
// tune pushed from the app, so a string typed on the badge and one sent over the wire
// cannot be judged by different standards.
//
// WHY A FILE AND NOT NodePrefs. `NodePrefs _prefs` is a resident member of `MyMesh`, and
// NodePrefs.h is compiled by ~60 companion variants. Eight slots held there would add
// ~1.9 KB of permanent RAM to every one of them, buzzer or not. In a file it costs flash
// instead, against the 100 KB the badge's extrafs ldscript has already given up to the
// filesystem, and only the tune currently sounding occupies RAM.
//
// WHY ONE FILE PER SLOT, and not one file of eight lines as this header first said.
// Measured after that first commit and it changed the answer: the badge's secondary
// filesystem is `CustomLFS ExtraFS(0xD4000, 0x19000, 128)` -- 100 KB at a 128-BYTE block
// -- so eight small files cost a few KB rather than the ~32 KB a 4 KB block would have
// implied. At that price per-slot files are simpler and safer: no read-modify-write, no
// temporary file, no rename (DataStore.cpp:462 already records that rename will not
// replace an existing file), and a power loss mid-write costs one slot instead of eight.
// The paths live in ui-new/TuneSlotStore.h with the file I/O.
//
// A packed binary record was never the alternative: the length-serialized records
// (ContactInfo, ChannelDetails) misparse the remainder of a list when a record's size
// changes, which is the trap this whole feature was kept clear of.

#include <stddef.h>
#include <stdint.h>

#include "Tunes.h"

namespace offband {

// Eight, per the owner. The count is bounded by the picker and by patience, not by
// storage: the tune list shows seven entries in one page at the large face, every
// arrow-press auditions, and nobody types a 218-character string sixteen times. Ids
// 16-254 stay free, so raising this renumbers nothing.
constexpr uint8_t kSlotCount = 8;

// Slots begin where the builtins end. Tied by assertion rather than by a comment,
// because a builtin added without moving this would silently alias a slot onto it.
constexpr uint8_t kFirstSlotId = kBuiltinCount;
static_assert(kFirstSlotId == kBuiltinCount, "slot ids must start after the builtins");
static_assert((int)kFirstSlotId + (int)kSlotCount <= (int)kTuneUnset,
              "the slot range must not reach the unset sentinel");

// The longest RTTTL a slot holds, terminator excluded. The owner's reference string from
// the badge maker is 218 characters; this leaves room above it without pretending to a
// limit nobody measured. 8 * 241 bytes is ~1.9 KB of filesystem.
constexpr size_t kMaxRtttl = 240;

// How much of a derived name a picker row can show. test_tone_slots caps an event label
// at 12 for the same reason: label plus value has to fit 21 columns.
constexpr size_t kMaxSlotName = 12;

inline bool isSlotId(uint8_t id) {
  return id >= kFirstSlotId && id < (uint8_t)(kFirstSlotId + kSlotCount);
}

// Which slot an id names. Only meaningful when isSlotId(id).
inline uint8_t slotIndex(uint8_t id) { return (uint8_t)(id - kFirstSlotId); }

inline uint8_t slotIdAt(uint8_t index) { return (uint8_t)(kFirstSlotId + index); }

// The name a slot shows, taken from the RTTTL's OWN name field -- everything before the
// first colon, trimmed and truncated.
//
// Derived rather than stored on purpose: one source of truth, so a stored name can never
// drift from the tune it labels, and typing the string is the whole of the work rather
// than the string plus a name.
//
// Returns the number of characters written. An empty result means "no usable name" --
// the caller shows the slot as empty or falls back, exactly as the builtin table's
// un-named entry is handled.
inline size_t deriveSlotName(const char* rtttl, char* out, size_t out_cap) {
  if (out == nullptr || out_cap == 0) return 0;
  out[0] = '\0';
  if (rtttl == nullptr) return 0;

  size_t start = 0;
  while (rtttl[start] == ' ' || rtttl[start] == '\t') start++;   // leading space is not a name

  size_t end = start;
  while (rtttl[end] != '\0' && rtttl[end] != ':') end++;
  // A string with no colon is not playable at all -- rtttlProblem() refuses it because
  // the player would walk past the buffer looking for one -- so it has no name either.
  if (rtttl[end] != ':') return 0;

  while (end > start && (rtttl[end - 1] == ' ' || rtttl[end - 1] == '\t')) end--;  // trailing

  size_t n = end - start;
  const size_t room = out_cap - 1 < kMaxSlotName ? out_cap - 1 : kMaxSlotName;
  if (n > room) n = room;
  for (size_t i = 0; i < n; i++) out[i] = rtttl[start + i];
  out[n] = '\0';
  return n;
}

// ---- a tune set on one channel (#1522) ------------------------------------------------
//
// Stored in a byte of /channels2 that has only ever held zero: the record is fixed-stride
// (4 unused + 32 name + 32 secret), and those four bytes are memset to zero on write and
// read-then-discarded on read. Taking one changes no stride, so DataStore::hasChannels's
// arithmetic stays correct and an older build still reads the file.
//
// THE ENCODING IS id + 1, AND THAT IS LOAD-BEARING.
//
// Tune id 0 is Silent -- a real choice someone makes. If a stored 0 meant Silent, then
// every channel in every /channels2 that already exists would go silent the moment a
// badge took this build, from a field that was only ever padding. With id + 1, an older
// file's zeros mean "unset", which falls back to the global channel tune and sounds
// exactly as it does today.
constexpr uint8_t kChannelTuneUnset = 0;

// The largest tune id that can be stored, since the encoding spends one value on "unset".
constexpr uint8_t kMaxStorableTuneId = 254;
static_assert((int)kFirstSlotId + (int)kSlotCount <= (int)kMaxStorableTuneId,
              "every builtin and slot id must fit the per-channel encoding");

// `id` as stored.
//
// Passing kTuneUnset is the CLEAR operation, not an error: it is how the picker's
// "Default" row gives a channel back to the global tune, and it is the only id outside
// the storable range, so there is no silent-failure set hiding behind it. Review read
// this as swallowing bad input; it is the intended way to un-set.
//
// The guard is written out rather than left to unsigned wraparound (0xFF + 1 == 0 would
// give the same answer) so the behaviour does not depend on overflow being convenient.
inline uint8_t encodeChannelTune(uint8_t id) {
  if (id > kMaxStorableTuneId) return kChannelTuneUnset;
  return (uint8_t)(id + 1);
}

// The stored byte as a tune id, or kTuneUnset when the channel has made no choice.
inline uint8_t decodeChannelTune(uint8_t stored) {
  if (stored == kChannelTuneUnset) return kTuneUnset;
  return (uint8_t)(stored - 1);
}

// Which tune a channel message plays: the channel's own choice, else what it inherits.
//
// `inherited` is what the event path already resolves today -- the global t_ch for
// ChannelMessage, itself falling back to DEFAULT_TONE_CHANNEL -- so an unset channel
// behaves exactly as it did before this existed, and the fallback chain has one new link
// at the front rather than a second mechanism beside it.
//
// A stored id this build cannot resolve (a slot from a build with more of them, or a
// corrupt byte) also falls back. It must NOT resolve to silence: silence is a choice
// someone makes deliberately, and a channel going quiet because of a downgrade would be
// indistinguishable from that choice -- whereas inheriting is what every un-configured
// channel already does, so it is the one outcome that surprises nobody.
//
// Review argued the two cases should be distinguishable -- "nothing chosen" versus "chose
// something this build cannot read" -- and that collapsing them hides a downgrade. Worth
// saying what is actually at stake: the stored BYTE is preserved either way, so nothing
// is lost and a re-upgrade restores the choice. Only the interpretation falls back, and
// no caller today would act differently on the distinction. The property that makes that
// true is that a save must never normalise an unresolvable id away, which is a done-when
// on the persistence task rather than something this function can guarantee.
inline uint8_t resolveChannelTune(uint8_t stored, uint8_t inherited) {
  const uint8_t id = decodeChannelTune(stored);
  if (id == kTuneUnset) return inherited;
  if (!isBuiltinTune(id) && !isSlotId(id)) return inherited;
  return id;
}

}  // namespace offband
