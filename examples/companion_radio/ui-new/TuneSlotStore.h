#pragma once

// #1474: reading and writing the eight custom tune slots. Badge-scoped: this header is
// included only by the badge's screens, so the other ~60 variants neither compile it nor
// pay for it.
//
// THE STAGING BUFFER, AND WHY IT EXISTS.
//
// `NonBlockingRtttl` keeps the melody POINTER -- `buffer = iSongBuffer` at
// NonBlockingRtttl.cpp:150 -- and walks it forward across successive loop() calls. The
// string must therefore stay alive and unchanged for the whole of playback, not just for
// the begin() call. A slot read into a stack buffer would dangle the instant the reader
// returned, and the tone would then walk freed stack: it would have passed on every tune
// short enough to finish before that stack was reused, and failed on a long one in a
// room full of badges.
//
// So exactly one tune is in RAM at a time -- the one sounding -- in a function-local
// static. That keeps the owner's "not resident" intent (slots are not held in memory,
// and nothing went into NodePrefs, which ~60 variants compile) at a cost of ~240 bytes
// on one board rather than ~1.9 KB on sixty.
//
// THE CALLER'S CONTRACT. Review described this as a re-entrancy time bomb via an
// interrupt, which is not reachable here -- the only ISR in the tree is the button's edge
// capture (ui-orig/Button.cpp), which records an edge and plays nothing; everything that
// sounds runs from the main loop, `the_mesh.loop()` and `ui_task.loop()`. But it was
// right that the contract as first written described the wrong invariant.
//
// What actually matters is not "stop first". It is that **no `buzzer.loop()` runs between
// staging and playing**: the player only walks the pointer from `loop()`, so as long as
// the stage-and-play pair is in one call stack, the overwrite window is zero. Stopping
// first is merely sufficient.
//
//   stageSlotForPlayback(...)  then  buzzer.play(ptr, ...)   -- same call stack, no loop()
//
// Named `stageSlotForPlayback` rather than `load` so a caller cannot read it as a getter.
// Reading a slot's NAME deliberately does not touch this buffer -- see slotName().
//
// ONE FILE PER SLOT, decided on a measurement.
//
// The badge's secondary filesystem is `CustomLFS ExtraFS(0xD4000, 0x19000, 128)` in
// main.cpp: 100 KB at a 128-BYTE block size. Eight small files therefore cost a few KB,
// not the ~32 KB a 4 KB block would have implied -- so per-slot files are affordable, and
// they are simpler and safer than one file: no read-modify-write, no temporary file, and
// no rename. That last point is not hypothetical; DataStore.cpp:462 already records the
// footgun ("SPIFFS rename won't replace an existing file, so the old one goes first").
// A power loss mid-write also costs one slot instead of all eight.
//
// This supersedes the line-delimited single file that `TuneSlots.h` described one commit
// earlier. The measurement came after that commit and changed the answer.

#include <stddef.h>
#include <stdint.h>

#include <helpers/ui/TuneSlots.h>

#include "../DataStore.h"

namespace offband {

// Slots go on the SECONDARY filesystem when there is one -- the badge's 100 KB exists for
// exactly this kind of thing, and the internal FS is the smaller of the two and already
// holds prefs, identity, contacts, channels and messages. Falls back to the primary when
// a build has no secondary (`_fsExtra` is nullptr in DataStore's two-argument
// constructor), so this is correct on a board without EXTRAFS rather than silently
// writing nowhere.
inline FILESYSTEM* slotFs(DataStore* store) {
  if (store == nullptr) return nullptr;
  FILESYSTEM* fs = store->getSecondaryFS();
  return fs != nullptr ? fs : store->getPrimaryFS();
}

// "/tune0" .. "/tune7". By INDEX, not by tune id: the index is the stable concept, so
// moving kFirstSlotId (a builtin added) does not rename everybody's saved tunes.
inline bool slotPath(uint8_t index, char* out, size_t out_cap) {
  if (out == nullptr || out_cap < 8 || index >= kSlotCount) return false;
  out[0] = '/'; out[1] = 't'; out[2] = 'u'; out[3] = 'n';
  out[4] = 'e'; out[5] = (char)('0' + index); out[6] = '\0';
  return true;
}

// The tune a slot holds, staged for playback. Returns nullptr for an empty, missing or
// unreadable slot -- all of which mean the same thing to a caller, and all of which
// resolve to silence rather than to noise.
//
// The returned pointer is valid until the NEXT call. See the contract above.
// A slot's tune into the CALLER's buffer. #1477's editor loads through this rather than
// through the staging buffer, so opening the editor cannot disturb a tune that is
// sounding. Returns the length, or 0 for an empty, missing or unreadable slot -- all of
// which mean the same thing to every caller.
inline size_t readSlotRtttl(DataStore* store, uint8_t index, char* out, size_t out_cap) {
  if (out == nullptr || out_cap == 0) return 0;
  out[0] = '\0';
  FILESYSTEM* fs = slotFs(store);
  char path[12];
  if (fs == nullptr || !slotPath(index, path, sizeof(path))) return 0;

  File f = store->openRead(fs, path);
  if (!f) return 0;
  const int n = f.read((uint8_t*)out, out_cap - 1);
  f.close();
  if (n <= 0) return 0;
  out[n] = '\0';
  // A file written by hand, or recovered from a crash, may carry a trailing newline.
  size_t len = (size_t)n;
  while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) out[--len] = '\0';
  if (len == 0) return 0;
  // An unplayable string must not reach the player: rtttlProblem() exists because the
  // parser walks past the buffer on some malformed input. A corrupt slot reads as empty,
  // which the picker and the event path already handle.
  if (rtttlProblem(out) != RtttlProblem::Ok) return 0;
  return len;
}

// The one buffer reserved for playback. See the contract at the top: the player keeps
// the pointer, so whatever is handed to it must outlive the whole of playback and must
// not be edited underneath it.
inline char* playbackStage() {
  static char staged[kMaxRtttl + 1];
  return staged;
}

inline const char* stageSlotForPlayback(DataStore* store, uint8_t index) {
  char* staged = playbackStage();
  return readSlotRtttl(store, index, staged, kMaxRtttl + 1) > 0 ? staged : nullptr;
}

// Stage a string the caller holds -- #1477's editor auditioning what has been typed so
// far. It is COPIED rather than played in place, and that is the point: the editor's own
// buffer is about to be typed into, and the player walks its pointer across loop() calls,
// so playing the live buffer would have the tune mutate underneath it as the next key
// arrives. Returns nullptr for anything the validator refuses, so nothing unplayable
// reaches a parser that walks past the end on malformed input.
inline const char* stageTextForPlayback(const char* text) {
  if (text == nullptr || text[0] == 0) return nullptr;
  if (rtttlProblem(text) != RtttlProblem::Ok) return nullptr;
  char* staged = playbackStage();
  size_t n = 0;
  while (text[n] != 0 && n < kMaxRtttl) {
    staged[n] = text[n];
    n++;
  }
  staged[n] = 0;
  return staged;
}

// A slot's display name, WITHOUT touching the staging buffer, because a screen drawing
// the slot list while a tune is sounding must not clobber the tune being played.
//
// Only the name field is needed, and it sits before the first colon, so this reads a
// short prefix rather than the whole string.
inline size_t slotName(DataStore* store, uint8_t index, char* out, size_t out_cap) {
  if (out == nullptr || out_cap == 0) return 0;
  out[0] = '\0';
  FILESYSTEM* fs = slotFs(store);
  char path[12];
  if (fs == nullptr || !slotPath(index, path, sizeof(path))) return 0;

  File f = store->openRead(fs, path);
  if (!f) return 0;
  // #1474 review, real bug: this first read only kMaxSlotName + 1 characters, so a name
  // of 13 or more pushed its colon outside the prefix, deriveSlotName() found none, and a
  // perfectly good slot rendered as EMPTY -- the user's saved tune made invisible.
  //
  // 64 is chosen so every plausible name's colon is inside the prefix while the read
  // stays far below the whole string. The colon requirement is KEPT: without it a corrupt
  // file would show a named row that can never make a sound, which is the failure #1467
  // and #1468 were about. A name of 63+ characters therefore reads as empty, which is a
  // trade worth making in that direction.
  char head[65];
  const int n = f.read((uint8_t*)head, sizeof(head) - 1);
  f.close();
  if (n <= 0) return 0;
  head[n] = '\0';
  return deriveSlotName(head, out, out_cap);
}

// Whether a slot holds anything. A metadata lookup, NOT a read: review pointed out that
// the Tones screen's "Custom 3 of 8" row would otherwise open, read and parse eight files
// on every render, and that screen re-renders about once a second.
//
// `exists()` is sufficient because `saveSlotRtttl` never writes an empty file -- an empty
// string clears the slot instead -- so a file that exists holds a tune that passed the
// validator at save time.
// #1475 review: the UI was doing real filesystem work in two hot paths -- up to seven
// open/read/close per picker render (about once a second) and up to eight exists() per
// up/down keypress while stepping over unfilled slots. On LittleFS neither is free: an
// exists() walks metadata blocks, and the input cost is the one a person would feel as
// lag. Treating file I/O as free is the classic version of this mistake.
//
// Names change only when a slot is written, so they are cached. POD with no member
// initializers on purpose: a static POD is zero-initialised with no guard variable, so
// there is no thread-safe-static machinery on an embedded target, and `valid` starts 0.
struct SlotNameCache {
  char name[kSlotCount][kMaxSlotName + 1];
  uint8_t valid;
};

inline SlotNameCache& slotNameCache() {
  static SlotNameCache c;
  return c;
}

// Every writer calls this. #1478's app push goes through saveSlotRtttl too, so a tune
// arriving over the wire invalidates the cache the same way one typed on the badge does.
inline void invalidateSlotNames() { slotNameCache().valid = 0; }

// A slot's name, from the cache, filling it on first use. One pass of eight reads after
// a change, then nothing until the next write.
inline size_t slotNameCached(DataStore* store, uint8_t index, char* out, size_t out_cap) {
  if (out == nullptr || out_cap == 0) return 0;
  out[0] = '\0';
  if (index >= kSlotCount) return 0;
  SlotNameCache& c = slotNameCache();
  if (!c.valid) {
    for (uint8_t i = 0; i < kSlotCount; i++) {
      slotName(store, i, c.name[i], kMaxSlotName + 1);
    }
    c.valid = 1;
  }
  size_t n = 0;
  while (c.name[index][n] != '\0' && n < out_cap - 1) {
    out[n] = c.name[index][n];
    n++;
  }
  out[n] = '\0';
  return n;
}

// Filled means "has a name this build can show". A file that exists but cannot be
// labelled -- a name of 63+ characters, or a corrupt file -- counts as empty, which is
// the same answer the picker gives, so the count and the list cannot disagree.
inline bool slotIsFilled(DataStore* store, uint8_t index) {
  char name[kMaxSlotName + 1];
  return slotNameCached(store, index, name, sizeof(name)) > 0;
}

// How many slots hold something, for the "Custom  3 of 8" row.
inline uint8_t filledSlotCount(DataStore* store) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kSlotCount; i++) {
    if (slotIsFilled(store, i)) n++;
  }
  return n;
}

inline bool clearSlot(DataStore* store, uint8_t index) {
  FILESYSTEM* fs = slotFs(store);
  char path[12];
  if (fs == nullptr || !slotPath(index, path, sizeof(path))) return false;
  invalidateSlotNames();
  if (!fs->exists(path)) return true;   // already empty is success, not failure
  return store->removeFile(fs, path);
}

// Write a slot. An empty or null string clears it, so one call serves both and a caller
// cannot leave a half-written slot behind by choosing the wrong one.
//
// The SAME validator gates this as gates the keyboard (#1477) and the app push (#1478):
// divergent validation between routes is how one of them becomes the trusted path and
// the other a trap.
inline bool saveSlotRtttl(DataStore* store, uint8_t index, const char* rtttl) {
  if (rtttl == nullptr || rtttl[0] == '\0') return clearSlot(store, index);
  if (rtttlProblem(rtttl) != RtttlProblem::Ok) return false;

  size_t len = 0;
  while (rtttl[len] != '\0') {
    if (len >= kMaxRtttl) return false;    // refuse rather than truncate into nonsense
    // Printable ASCII only, which is exactly what LineEdit::apply accepts. Review found
    // that without this, load-then-save was not idempotent: a tab or newline pushed in
    // from the app (#1478) would come back through the editor as a space or be dropped,
    // silently rewriting a stored tune -- and a dropped interior newline could merge an
    // RTTTL's defaults into its notes and invalidate it. Refusing here makes both write
    // routes agree on what a slot may hold.
    const unsigned char ch = (unsigned char)rtttl[len];
    if (ch < 0x20 || ch > 0x7E) return false;
    len++;
  }

  FILESYSTEM* fs = slotFs(store);
  char path[12];
  if (fs == nullptr || !slotPath(index, path, sizeof(path))) return false;

  File f = store->openWriteFile(fs, path);
  if (!f) return false;
  const size_t wrote = f.write((const uint8_t*)rtttl, len);
  f.close();
  invalidateSlotNames();
  if (wrote != len) {
    // A short write leaves a truncated tune, which could be a string the validator would
    // refuse. Removing it is better than leaving the slot holding half a tune.
    clearSlot(store, index);
    return false;
  }
  return true;
}

// #1475: the label for ANY tune id, builtin or slot, into a caller buffer.
//
// One resolver so the picker, the Tones rows and the Settings summary cannot disagree
// about what a stored id is called. Writes an empty string for an id that names nothing
// -- an un-listed builtin, an empty slot, or an id past this build's table -- which the
// screens already render as their own fallback rather than as a blank row.
//
// Deliberately takes the caller's buffer: the staging buffer belongs to playback, and a
// screen asking for a label while a tune sounds must not disturb it.
inline size_t tuneLabel(DataStore* store, uint8_t id, char* out, size_t out_cap) {
  if (out == nullptr || out_cap == 0) return 0;
  out[0] = '\0';
  if (isSlotId(id)) return slotNameCached(store, slotIndex(id), out, out_cap);

  const char* name = tuneName(id);
  if (name == nullptr) return 0;
  size_t n = 0;
  while (name[n] != '\0' && n < out_cap - 1) {
    out[n] = name[n];
    n++;
  }
  out[n] = '\0';
  return n;
}

// Is this id something the picker should offer? A builtin with a name, or a filled slot.
inline bool tuneIsListed(DataStore* store, uint8_t id) {
  if (isSlotId(id)) return slotIsFilled(store, slotIndex(id));
  return tuneName(id) != nullptr;
}

// Which picker row to open on, now that slots exist (#1475).
//
// The pure `pickerRowFor()` in Tunes.h predates slots: it sends anything past the builtin
// table to row 0, which would have opened the picker on Silent for a person whose DM tune
// IS a slot -- showing them the wrong current tune and auditioning the wrong thing. It
// cannot be fixed there, because deciding whether a slot is filled needs the filesystem.
//
// Same rule as the pure one, widened: open on the tune in use when it is something the
// picker lists, and on Silent when it is not. Silent is the safe fallback for the same
// reason as before -- audible as nothing, visible as "Silent", never a surprise.
inline uint8_t pickerRowFor(DataStore* store, uint8_t effective_id) {
  return tuneIsListed(store, effective_id) ? effective_id : kTuneSilent;
}

// kPickerIdEnd moved to TuneSlots.h in #1525, where the picker's row mapping lives and
// can be unit-tested. It is the same value; nothing that used it had to change.

}  // namespace offband
