#pragma once

// #1418: the tunes a badge can play, and whether a string is one.
//
// Every tone used to be an RTTTL literal at its call site, so nothing could choose
// between them. Here they are a table, and a choice is a `uint8_t` id -- which is what
// a pref stores, what a per-source override stores, and what the remote trigger names.
//
// IDS ARE APPEND-ONLY AND ARE NEVER RENUMBERED. A badge in the field has ids in its
// prefs; renumbering silently re-points someone's chosen tune at a different melody.
// New tunes go on the end. Id 0 is silent, so an event can be turned off on its own
// without muting the badge.
//
// A null `name` marks an entry the picker does not list. That is the whole mechanism
// behind a tune only a trigger can reach (#1422): it is reachable by id and invisible
// in the menu.
//
// Ids at or above `kBuiltinCount` are custom slots -- strings typed on the badge
// (#1413). They do not live here; `tuneRtttl()` resolves builtins only and says so by
// returning nullptr, which is the caller's cue to look in the slots.
//
// Pure: no Arduino, no RTTTL library, no mesh. Host-tested in test/test_tunes.

#include <stddef.h>
#include <stdint.h>

namespace offband {

struct Tune {
  const char* name;   // what the picker shows; nullptr means it does not list it
  const char* rtttl;  // the string the buzzer takes; nullptr for silence
};

// Silence. Not a melody -- `tuneRtttl(kTuneSilent)` returns nullptr and the caller
// plays nothing, which is how an event is turned off individually.
constexpr uint8_t kTuneSilent = 0;

// THE TABLE. Append only.
//
// Six chimes, owner-approved 2026-10-14 (#1447). They are written for the purpose
// rather than transcribed from melodies, for two reasons. Measured: nothing available
// as RTTTL in the wild is a notification tone -- the badge maker's own example runs
// 17.1 s over 66 notes, and the shortest thing found elsewhere was 3.1 s, against the
// 0.06-0.19 s of the tones already here. And nothing is borrowed, so no question of
// another project's license or of a melody's trademark touches any of it. Recognizable
// melodies belong in the custom slots (#1413), which is also where a long one belongs.
//
// The contours are deliberately different KINDS of thing -- rising-four, falling-pair,
// single, rising-pair, falling-three, repeated-same -- because the point is to know
// what buzzed without looking. Kerplop and Bloop are a matched pair, the same two-note
// idea inverted, which reads as channel versus DM.
//
// The first three are exactly what the badge played before any of this, so an
// un-configured badge sounds unchanged; this only adds choices.
constexpr Tune kBuiltinTunes[] = {
    {"Silent", nullptr},
    // Was UITask::notify()'s contactMessage literal. Rising four, "arrival". 0.19 s.
    {"Chime", "MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7"},
    // Was channelMessage. Falling pair, tight. 0.12 s.
    {"Kerplop", "kerplop:d=16,o=6,b=120:32g#,32c#"},
    // Was ack. One note, deliberately: it fires often. 0.06 s.
    {"Tick", "ack:d=32,o=8,b=120:c"},
    // Rising pair, low -- Kerplop inverted. 0.17 s.
    {"Bloop", "Bloop:d=16,o=5,b=180:g,d6"},
    // Falling three, "going away". 0.25 s.
    {"Drop", "Drop:d=16,o=6,b=180:c7,g,e"},
    // Two of the same pitch with a gap between them. 0.21 s.
    {"Knock", "Knock:d=32,o=6,b=140:a,16p,a"},
    // #1422: THE UN-LISTED ONE. A null name keeps it out of the picker, so it exists
    // only for something that names it by id. Nothing in the UI can reach or select it.
    //
    // THE PAYLOAD, supplied by the owner 2026-10-18. It is his reference string from the
    // badge maker -- the same 218-character line kMaxRtttl in TuneSlots.h was sized
    // around -- and it runs 17.1 s over 66 tokens (53 notes, 13 rests, octaves 5-6).
    //
    // That length is why TonePriority::Trigger exists (#1608): at 17 s against the
    // 0.06-0.25 s of every notification above, an Event-priority play would be truncated
    // by the next arriving message, which in a busy channel is every time.
    //
    // The line it replaced was a placeholder of our own. The owner has accepted
    // the risk of shipping a recognizable tune here (#1447 discussion), so this line is
    // his to replace -- or, once typed custom slots land (#1413), the trigger can name a
    // slot he filled in himself and the repo never carries the payload at all.
    {nullptr, "n:d=4,o=5,b=200:8g,8a,8c6,8a,e6,8p,e6,8p,d6.,p,8p,8g,8a,8c6,8a,d6,8p,d6,8p,c6,8b,a.,8g,8a,8c6,8a,2c6,d6,b,a,g.,8p,g,2d6,2c6.,p,8g,8a,8c6,8a,e6,8p,e6,8p,d6.,p,8p,8g,8a,8c6,8a,2g6,b,c6.,8b,a,8g,8a,8c6,8a,2c6,d6,b,a,g.,8p"},
};

constexpr uint8_t kBuiltinCount = (uint8_t)(sizeof(kBuiltinTunes) / sizeof(kBuiltinTunes[0]));

// Names for the three ids the event defaults below point at, so a default is written as
// a name rather than a position. Append-only applies to these too: they are the ids a
// shipped badge already has in its prefs.
constexpr uint8_t kTuneChime = 1;    // the old contactMessage literal
constexpr uint8_t kTuneKerplop = 2;  // the old channelMessage literal
constexpr uint8_t kTuneTick = 3;     // the old ack literal
constexpr uint8_t kTuneBloop = 4;    // #1447
constexpr uint8_t kTuneDrop = 5;
constexpr uint8_t kTuneKnock = 6;
// #1422: the un-listed entry. Reachable by id, invisible in the picker. Named here so a
// caller refers to it by meaning rather than by position -- ids are append-only, and a
// literal 7 at a call site would be the one thing a future insertion could not fix.
constexpr uint8_t kTuneTrigger = 7;

static_assert(kBuiltinCount > 0, "the table needs an entry");
static_assert(kBuiltinCount < 255, "tune ids are 8-bit and 255 is kept free");
static_assert(kBuiltinTunes[kTuneSilent].rtttl == nullptr, "id 0 is silence");
static_assert(kTuneTrigger < kBuiltinCount, "the named ids must be in the table");
static_assert(kBuiltinTunes[kTuneTrigger].name == nullptr,
              "the trigger entry must stay out of the picker");
static_assert(kBuiltinTunes[kTuneTrigger].rtttl != nullptr,
              "a trigger that plays nothing is not a trigger");
static_assert(kBuiltinTunes[kTuneChime].rtttl != nullptr, "Chime needs a melody");
static_assert(kBuiltinTunes[kTuneKerplop].rtttl != nullptr, "Kerplop needs a melody");
static_assert(kBuiltinTunes[kTuneTick].rtttl != nullptr, "Tick needs a melody");
static_assert(kBuiltinTunes[kTuneBloop].rtttl != nullptr, "Bloop needs a melody");
static_assert(kBuiltinTunes[kTuneDrop].rtttl != nullptr, "Drop needs a melody");
static_assert(kBuiltinTunes[kTuneKnock].rtttl != nullptr, "Knock needs a melody");

// ---------------------------------------------------------------------------------
// Which tune each event gets.
//
// The UI's own event enum lives beside the UI task, which a pure header cannot reach,
// so the events are named again here and the UI translates. That keeps the mapping and
// its defaults testable on the host, which is where "does every event have a slot" and
// "do the defaults reproduce today's behavior" get answered.
//
// #1420 replaces `defaultTuneFor` as the SOURCE of the id with a stored pref. These
// stay as the value a badge takes when it has no pref yet, which is why they are
// per-event overridable: a board sets its own starting point without touching code.
// ---------------------------------------------------------------------------------

enum class ToneEvent : uint8_t {
  ContactMessage = 0,
  ChannelMessage,
  RoomMessage,
  NewContact,
  Ack,
  Sent,
  Count,
};

// Defaults reproduce EXACTLY what the firmware does today, which is why room and
// new-contact messages are silent: their cases fall through the UI's switch with no
// sound, and giving them one here would be a behavior change on every board that
// compiles this UI rather than a new setting.
#ifndef DEFAULT_TONE_CONTACT
  #define DEFAULT_TONE_CONTACT  offband::kTuneChime
#endif
#ifndef DEFAULT_TONE_CHANNEL
  #define DEFAULT_TONE_CHANNEL  offband::kTuneKerplop
#endif
#ifndef DEFAULT_TONE_ROOM
  #define DEFAULT_TONE_ROOM     offband::kTuneSilent
#endif
#ifndef DEFAULT_TONE_NEWCONTACT
  #define DEFAULT_TONE_NEWCONTACT  offband::kTuneSilent
#endif
#ifndef DEFAULT_TONE_ACK
  #define DEFAULT_TONE_ACK      offband::kTuneTick
#endif
// Silent fleet-wide: no board plays anything on send today. The badge env turns it on,
// because the badge is the board the owner asked for a send confirmation on, and the
// badge is also the only board that can fire this event at all.
#ifndef DEFAULT_TONE_SENT
  #define DEFAULT_TONE_SENT     offband::kTuneSilent
#endif

// #1420: "no choice stored yet", which is NOT the same as choosing Silent.
//
// Silence is a real choice with a real id (0), so a stored 0 has to mean the human
// asked for silence. That leaves no room for "unset" at the bottom, hence a sentinel at
// the top -- which the `kBuiltinCount < 255` assertion above has been keeping free.
//
// Why this matters rather than being tidiness: a badge that has never opened Settings
// takes whatever the firmware's default is, and that default can be improved in a later
// build. If "unset" were 0, every un-configured badge would be permanently silent, and
// the two states would be indistinguishable forever after.
constexpr uint8_t kTuneUnset = 0xFF;
static_assert(kTuneUnset >= kBuiltinCount, "the unset sentinel must not name a tune");

// The id to actually play: what was stored, or this event's compiled default when
// nothing has been stored. An out-of-range stored id is left alone here -- `tuneRtttl`
// turns it into silence, and #1413 will resolve it as a custom slot.
inline uint8_t resolveTune(uint8_t stored, ToneEvent e);

inline uint8_t defaultTuneFor(ToneEvent e) {
  switch (e) {
    case ToneEvent::ContactMessage: return DEFAULT_TONE_CONTACT;
    case ToneEvent::ChannelMessage: return DEFAULT_TONE_CHANNEL;
    case ToneEvent::RoomMessage:    return DEFAULT_TONE_ROOM;
    case ToneEvent::NewContact:     return DEFAULT_TONE_NEWCONTACT;
    case ToneEvent::Ack:            return DEFAULT_TONE_ACK;
    case ToneEvent::Sent:           return DEFAULT_TONE_SENT;
    case ToneEvent::Count:          break;
  }
  return kTuneSilent;  // an event with no slot is silent, never an arbitrary tune
}

inline uint8_t resolveTune(uint8_t stored, ToneEvent e) {
  return stored == kTuneUnset ? defaultTuneFor(e) : stored;
}

// The RTTTL a builtin id names, or nullptr for silence, for an id past the table, or
// for an entry with no melody. A caller never indexes the table itself.
//
// nullptr is NOT an error signal on its own: a custom-slot id is legitimately past the
// table. Use `isBuiltinTune()` to tell the two apart.
inline const char* tuneRtttl(uint8_t id) {
  if (id >= kBuiltinCount) return nullptr;
  return kBuiltinTunes[id].rtttl;
}

inline bool isBuiltinTune(uint8_t id) { return id < kBuiltinCount; }

// Which picker row to open on, given a BUILTIN tune an event resolves to (#1467).
//
// Renamed from pickerRowFor in #1475, when a store-aware overload arrived in
// ui-new/TuneSlotStore.h: two functions of the same name, one of which silently sends
// every custom slot to Silent, is a trap for whoever reaches for the shorter one. This
// one is correct only where slots cannot occur.
//
// Why this is a rule and not an expression at the call site: the picker auditions
// whatever it opens on, so "which row do we open on" decides what gets PLAYED, and an
// un-named entry (#1422) must never be played by the UI -- that is the whole point of
// leaving it un-named. The navigation path already steps over un-named entries; opening
// has to honor the same rule, or the one tune the UI is not allowed to reach becomes
// reachable by simply having it stored.
//
// Nothing can store an un-named id today -- the picker is the only writer and it skips
// them. The guard is here anyway, because anything that names id 7 by id, and #1413
// (custom slots), both add writers, and a rule that holds only while no writer exists is
// not a rule.
//
// Falls back to row 0, which is Silent: audible as nothing, visible as "Silent", and
// never a surprise.
inline uint8_t pickerRowForBuiltin(uint8_t effective_id) {
  if (effective_id >= kBuiltinCount) return 0;
  if (kBuiltinTunes[effective_id].name == nullptr) return 0;
  return effective_id;
}

// The name the picker shows, or nullptr when this id is not one the picker lists --
// either because it is past the table or because the entry is deliberately un-named.
inline const char* tuneName(uint8_t id) {
  if (id >= kBuiltinCount) return nullptr;
  return kBuiltinTunes[id].name;
}

// How many builtins the picker lists. Un-named entries are skipped, so this is not
// `kBuiltinCount`.
inline uint8_t visibleTuneCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kBuiltinCount; i++) {
    if (kBuiltinTunes[i].name != nullptr) n++;
  }
  return n;
}

// The id of the nth listed builtin, counting only those the picker shows. Returns
// `kTuneSilent` when `nth` is past the end, so a caller cannot walk off the table.
inline uint8_t visibleTuneId(uint8_t nth) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < kBuiltinCount; i++) {
    if (kBuiltinTunes[i].name == nullptr) continue;
    if (n == nth) return i;
    n++;
  }
  return kTuneSilent;
}

// ---------------------------------------------------------------------------------
// Is this string safe to hand the player?
//
// Needed in two places: over this table as a test, and over a string someone typed on
// the badge's keyboard before it is stored (#1413). Typos are the normal case there.
//
// THIS MIRRORS THE PLAYER, NOT THE RTTTL SPEC. The player is
// `NonBlockingRtttl::begin()`, and what it actually does decides what is safe:
//
//   `while(*buffer != ':') buffer++;`   -- a string with NO colon walks off the end of
//                                          the buffer. Rejecting that is the one check
//                                          here that prevents an out-of-bounds read.
//   `if(*buffer == 'd') {...}`          -- each default is OPTIONAL, read POSITIONALLY,
//   `if(*buffer == 'o') {...}`             in the order d, o, b, immediately after the
//   `if(*buffer == 'b') {...}`             first colon. Not a free-form scan.
//   octave: `num = *buffer++ - '0';`    -- exactly ONE digit. `o=10` makes it eat the
//                                          '0' as a separator and mis-parse the rest.
//   each default ends `buffer++;`       -- it skips ONE character blindly, so whatever
//                                          follows the digits must be a separator or a
//                                          real note character is swallowed.
//   `wholenote = (60 * 1000L / bpm) * 4;` -- **`b=0` DIVIDES BY ZERO.** The player does
//                                          not guard it, so this validator must.
//
// Two consequences worth stating, because neither follows from reading the spec:
//
//  * Defaults really are optional -- `Song:c,e,g` plays. An earlier version of this
//    function demanded all three and would have rejected a large, valid, working
//    subset of real RTTTL.
//  * But a defaults-less tune whose notes START with `d`, `o` or `b` is mis-parsed,
//    because the player cannot tell the note from a default key. `Song:b,c,d` is the
//    bad one: the player takes `b` for `b=`, reads no digits, sets bpm 0, and divides
//    by zero. So that shape is refused rather than played.
//
// It deliberately does NOT validate individual notes. The player tolerates odd ones,
// and being stricter here would reject strings that play fine.
// ---------------------------------------------------------------------------------

enum class RtttlProblem : uint8_t {
  Ok = 0,
  Empty,          // null or zero-length
  NoName,         // nothing before the first colon
  MissingColon,   // no colon at all -- the player would read past the buffer
  BadDefault,     // d=, o= or b= present but malformed, or not followed by a separator
  ZeroTempo,      // b=0, or b= with no digits: the player divides by zero
  NoNotes,        // nothing after the header
  AmbiguousNote,  // no defaults, and the notes start with d, o or b
  StrayColon,     // a colon the player never consumes, so it plays as a note
};

namespace detail {

inline bool isDigit(char c) { return c >= '0' && c <= '9'; }

// The player ends every default by skipping one character blindly, so the character
// after the digits has to be a separator or a note gets eaten.
inline bool isDefaultsSep(char c) { return c == ',' || c == ':'; }

}  // namespace detail

// Why `rtttl` is not safe to play, or Ok. A nullptr melody is Empty, which is why
// `tuneRtttl(kTuneSilent)` must not be passed here -- silence is not a melody.
inline RtttlProblem rtttlProblem(const char* rtttl) {
  if (rtttl == nullptr || *rtttl == 0) return RtttlProblem::Empty;

  const char* p = rtttl;
  while (*p && *p != ':') p++;
  if (*p == 0) return RtttlProblem::MissingColon;  // the out-of-bounds walk
  if (p == rtttl) return RtttlProblem::NoName;
  p++;  // past the first colon, where the player starts looking for defaults

  bool saw_default = false;

  // Each default is recognized only as `<key>=`. A bare `d`, `o` or `b` is a NOTE, and
  // falling through to the ambiguity check below is how it gets the right message
  // rather than being reported as a malformed default.
  //
  // d=<digits><sep>, optional. The player ignores a zero or absent value and keeps 4,
  // so only the shape and an overflow-sized number matter here.
  if (*p == 'd' && p[1] == '=') {
    p += 2;
    int digits = 0;
    while (detail::isDigit(*p)) { p++; digits++; }
    if (digits == 0 || digits > 4) return RtttlProblem::BadDefault;
    if (!detail::isDefaultsSep(*p)) return RtttlProblem::BadDefault;
    p++;
    saw_default = true;
  }

  // o=<one digit><sep>, optional. Exactly one digit: the player reads a single
  // character and then skips the next one.
  if (*p == 'o' && p[1] == '=') {
    if (!detail::isDigit(p[2])) return RtttlProblem::BadDefault;
    if (!detail::isDefaultsSep(p[3])) return RtttlProblem::BadDefault;
    p += 4;
    saw_default = true;
  }

  // b=<digits><sep>, optional. The value feeds a division, so zero is fatal.
  if (*p == 'b' && p[1] == '=') {
    p += 2;
    int digits = 0;
    long bpm = 0;
    while (detail::isDigit(*p)) {
      if (digits < 5) bpm = bpm * 10 + (*p - '0');
      p++;
      digits++;
    }
    if (digits == 0) return RtttlProblem::ZeroTempo;  // b= with nothing: bpm stays 0
    if (digits > 4) return RtttlProblem::BadDefault;
    if (bpm == 0) return RtttlProblem::ZeroTempo;
    if (!detail::isDefaultsSep(*p)) return RtttlProblem::BadDefault;
    p++;
    saw_default = true;
  }

  if (*p == 0) return RtttlProblem::NoNotes;

  // With no defaults consumed, the player would mistake a leading d, o or b note for a
  // default key. For `b` that ends in its divide by zero.
  if (!saw_default && (*p == 'd' || *p == 'o' || *p == 'b')) {
    return RtttlProblem::AmbiguousNote;
  }

  // Everything from here is what the player calls `firstNote`, and notes never contain
  // a colon. One left over means the player did NOT consume the header the writer
  // intended -- a space before the defaults, or a colon inside the name -- and it will
  // play the rest of the header as notes. Catching the leftover catches both causes.
  for (const char* q = p; *q; q++) {
    if (*q == ':') return RtttlProblem::StrayColon;
  }

  return RtttlProblem::Ok;
}

inline bool rtttlLooksValid(const char* rtttl) {
  return rtttlProblem(rtttl) == RtttlProblem::Ok;
}

// Short words for a screen. Kept here so the picker and the slot editor say the same
// thing about the same problem. Each fits a badge row.
inline const char* rtttlProblemText(RtttlProblem p) {
  switch (p) {
    case RtttlProblem::Ok: return "OK";
    case RtttlProblem::Empty: return "Empty";
    case RtttlProblem::NoName: return "Needs a name";
    case RtttlProblem::MissingColon: return "Needs name:notes";
    case RtttlProblem::BadDefault: return "Bad d=, o= or b=";
    case RtttlProblem::ZeroTempo: return "b= must be 1 or more";
    case RtttlProblem::NoNotes: return "No notes";
    case RtttlProblem::AmbiguousNote: return "Add d=, o= or b= first";
    case RtttlProblem::StrayColon: return "Bad name or d=/o=/b=";
  }
  return "Bad tune";
}

}  // namespace offband
