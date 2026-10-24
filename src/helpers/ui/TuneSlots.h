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

// ---- the picker's rows (#1525) --------------------------------------------------------

// One past the last id the picker may show, so a screen does not hard-code the sum.
// Lived in ui-new/TuneSlotStore.h until #1525 needed a row mapping that could be
// unit-tested; it has no filesystem dependency and belongs here with the id space.
constexpr uint8_t kPickerIdEnd = kFirstSlotId + kSlotCount;

// The picker lists one extra row, at the top, when it is editing a CONVERSATION rather
// than an event: Default.
//
// That row is the only way to un-set a tune a conversation has pinned. Without it the
// picker could change the choice but never remove it, and a conversation would be stuck
// with whatever was chosen once. Its id is kTuneUnset, because that is already precisely
// what it means and because encodeChannelTune turns it into the stored "no choice".
//
// Rows rather than ids, with the mapping in one place, because the alternative is a
// selection index that means a tune id in one mode and a row in the other -- the trap
// that got pickerRowFor renamed in #1475.
constexpr int kTonePickerDefaultRow = 0;

inline int tonePickerRowCount(bool with_default) {
  return (int)kPickerIdEnd + (with_default ? 1 : 0);
}

// The tune a row offers, or kTuneUnset for the Default row and for a row out of range.
inline uint8_t tonePickerIdAt(bool with_default, int row) {
  if (with_default) {
    if (row == kTonePickerDefaultRow) return kTuneUnset;
    row--;
  }
  if (row < 0 || row >= (int)kPickerIdEnd) return kTuneUnset;
  return (uint8_t)row;
}

// Where an id sits, or -1 when this picker has no row for it. kTuneUnset has a row only
// when the Default row is shown, which is what makes "un-set" unreachable for an event:
// an event always plays something, so it has no Default to fall back to.
inline int tonePickerRowFor(bool with_default, uint8_t id) {
  if (id == kTuneUnset) return with_default ? kTonePickerDefaultRow : -1;
  if ((int)id >= (int)kPickerIdEnd) return -1;
  return with_default ? (int)id + 1 : (int)id;
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
// constexpr so a seed table can encode at compile time and static_assert itself against
// this function rather than hard-coding the +1 (variants/qcc_badge/QccChannels.h, #1625).
//
// Written as ONE return on purpose: the nRF52 toolchain builds this at C++11, where a
// constexpr body must be a single return statement. An `if` here compiles everywhere else
// and fails only on the boards this header actually ships to.
constexpr uint8_t encodeChannelTune(uint8_t id) {
  return (id > kMaxStorableTuneId) ? kChannelTuneUnset : (uint8_t)(id + 1);
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
// saying what is actually at stake: the stored BYTE is preserved, so only the
// interpretation falls back, and a build that does know the id finds the choice intact.
// No caller today would act differently on the distinction. The property that makes that
// true is that a save must never normalise an unresolvable id away, which lives with the
// persistence (channelTuneToSpare) rather than here.
//
// It does NOT extend to a downgrade below the build that introduced the field at all --
// that one zeroes the spare bytes on its first save and the tunes are gone. See the note
// on channelTuneToSpare; #1523 records it as the accepted cost.
inline uint8_t resolveChannelTune(uint8_t stored, uint8_t inherited) {
  const uint8_t id = decodeChannelTune(stored);
  if (id == kTuneUnset) return inherited;
  if (!isBuiltinTune(id) && !isSlotId(id)) return inherited;
  return id;
}

// May a client store `stored` as a channel's tune (#1789)?
//
// The companion protocol is a second writer of this byte, beside the badge's picker, and a
// second writer must accept exactly what the picker offers -- no more. A range check is
// not that: the builtin table has an entry the picker deliberately does not list, and a
// range check would hand the wire a way to set it. So the rule is the picker's own:
// Default (unset), every builtin the picker names, and every custom slot.
//
// Anything else is refused rather than clamped, so a client that sent a bad id learns it
// instead of finding some other tune stored in its place.
inline bool channelTuneSettable(uint8_t stored) {
  if (stored == kChannelTuneUnset) return true;
  const uint8_t id = decodeChannelTune(stored);
  if (isSlotId(id)) return true;
  return tuneName(id) != nullptr;
}

// ---- where it sits in the file (#1523) ------------------------------------------------
//
// Each /channels2 record is 4 spare bytes, then 32 of name, then 32 of secret. The spare
// field has only ever been written as zero and read-then-discarded, so taking one byte of
// it changes no offset: the stride stays 68, DataStore::hasChannels's hardcoded
// 4 + 32 + 32 stays right, and an older build reads the file as it always did.
constexpr size_t kChannelSpareBytes = 4;
constexpr size_t kChannelTuneSpareIndex = 0;
static_assert(kChannelTuneSpareIndex < kChannelSpareBytes, "the tune must fit the spare field");

// The stored tune byte out of a record's spare field.
inline uint8_t channelTuneFromSpare(const uint8_t* spare) {
  if (spare == nullptr) return kChannelTuneUnset;
  return spare[kChannelTuneSpareIndex];
}

// The spare field as it goes back to the file.
//
// `stored` is written through BYTE-FOR-BYTE and is deliberately not validated here. A
// build that reads a tune id it cannot resolve must write that same byte back: #1522's
// resolution falls back to the inherited tune, and the byte surviving every save THIS
// build makes is what keeps that a fallback rather than a loss -- a later build that
// knows the id finds the choice still there. Normalising an unresolvable id to "unset"
// here is what would actually destroy it.
//
// That argument covers one case only, and review was right to press on it: a build that
// HAS this field but cannot resolve the id. A downgrade to a build from before the field
// existed is different -- its saveChannels zeroes all four spare bytes, so the first save
// it makes drops every per-channel tune, and a re-upgrade finds them unset. That is this
// design's one real cost and it is recorded as accepted on #1523. Reading on an older
// build is harmless; it is writing that loses them.
//
// The remaining three bytes are zeroed, exactly as the memset they replace did, so a
// later field added beside this one starts from a known value rather than from whatever
// this build happened to leave there.
inline void channelTuneToSpare(uint8_t stored, uint8_t* spare) {
  if (spare == nullptr) return;
  for (size_t i = 0; i < kChannelSpareBytes; i++) spare[i] = 0;
  spare[kChannelTuneSpareIndex] = stored;
}

// What a channel's tune becomes when the app overwrites the slot.
//
// CMD_SET_CHANNEL carries a name and a 16-byte secret, nothing else, and the handler
// builds a fresh record -- so an edit from the phone keeps only what is carried over
// deliberately. Without this, renaming a channel on the phone would silently clear the
// tune set on the badge.
//
// It keeps the tune only when the slot still holds the SAME CHANNEL, judged by the secret
// rather than by the slot being occupied. Review caught the difference: channel slots are
// a fixed table and the app reuses them, so "the slot was not empty" is also true when
// someone has replaced channel A with channel B in it -- and B would then inherit a tune
// chosen for a channel that is gone. The secret is what identifies a channel, so a
// re-key counts as a different channel too; a rename, which is what people actually do,
// keeps its tune.
//
// Note this is DELIBERATELY stricter than the per-channel notification mode beside it
// (#1457), which keys off occupancy alone and so does carry a mode across a replacement.
// Filed rather than quietly changed, since that is shipped behaviour and not this task.
//
// A rule in its own function because the alternative is a condition inside an
// Arduino-only translation unit, where the cost of getting it wrong is every channel on
// the badge silently losing its tune the next time someone edits one from the phone.
inline uint8_t channelTuneAfterEdit(bool slot_was_empty, bool same_channel,
                                    uint8_t before_stored) {
  if (slot_was_empty || !same_channel) return kChannelTuneUnset;
  return before_stored;
}

}  // namespace offband
