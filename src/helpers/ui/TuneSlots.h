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
// WHY LINE-DELIMITED TEXT AND NOT A PACKED RECORD. The project's own trap is that the
// length-serialized binary records (ContactInfo, ChannelDetails) misparse the remainder
// of a list when a record's size changes. Lines have no lengths and no fixed widths: a
// build with fewer slots ignores extra lines, one with more reads missing lines as empty,
// and the file can be read by eye on a badge that is misbehaving at a conference. RTTTL
// contains no newline, so the delimiter is unambiguous.

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

// Line `index` of a text blob, copied out and terminated. Trailing \r is dropped so a
// file that has been through a Windows editor still reads correctly.
//
// Returns the number of characters written. A line that does not exist, or an empty one,
// both give 0 -- an absent slot and a blank slot are the same thing, so the caller needs
// no third case.
inline size_t copyLineAt(const char* blob, size_t len, size_t index, char* out,
                         size_t out_cap) {
  if (out == nullptr || out_cap == 0) return 0;
  out[0] = '\0';
  if (blob == nullptr) return 0;

  size_t i = 0, line = 0;
  while (i < len && line < index) {
    if (blob[i] == '\n') line++;
    i++;
  }
  if (line < index) return 0;   // the blob ended before that line

  size_t end = i;
  while (end < len && blob[end] != '\n') end++;
  if (end > i && blob[end - 1] == '\r') end--;

  size_t n = end - i;
  if (n > out_cap - 1) n = out_cap - 1;
  for (size_t k = 0; k < n; k++) out[k] = blob[i + k];
  out[n] = '\0';
  return n;
}

}  // namespace offband
