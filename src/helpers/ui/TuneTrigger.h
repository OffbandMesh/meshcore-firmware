#pragma once

// Whether an arriving message asks this badge to sound the un-listed tune at
// `kTuneTrigger`.
//
// WHAT THIS FILE IS. The matching rule only -- no Arduino, no mesh, no buzzer -- so every
// branch of it is walked by host tests. Deciding WHICH messages are even offered to it
// (the channel, or a DM) is the caller's job, because that needs the mesh layer; see the
// receive handlers in examples/companion_radio/MyMesh.cpp.
//
// FOUR CONDITIONS, AND ALL FOUR ARE DELIBERATE. Two of them look redundant and are not:
//
//   - the sender's name ends with the mark below
//   - the body carries `kTriggerDecoy`
//   - the body ENDS with the Morse for the 4th letter of the sender's name
//   - (caller) the message came in on the right channel, or as a DM
//
// The third is derived PER SENDER, which is the whole point of it: the second is
// conspicuous and identical on every triggering message, so it is what anyone comparing
// messages will find, while the third cannot be copied off somebody else's traffic because
// the expected pattern follows from their name and not from the message. Dropping either
// one changes the behavior on purpose. A maintainer tidying "the duplicate marker" away
// would quietly turn a derived key into a fixed one.
//
// Everything here is printable ASCII on the wire except the sender's mark. That is also
// deliberate and was arrived at the hard way: a marker made of trailing whitespace is
// deleted by the client's compose box (`String.trim()`), and one made of zero-width
// characters cannot be typed on the badge at all, because `LineEdit` accepts 0x20..0x7E.
// Dots and dashes survive both and can be typed anywhere.

#include <stddef.h>
#include <stdint.h>

#include "MorseBlink.h"   // morseFor()

namespace offband {

// The mark a sender's name ends with. U+1F6F0, optionally followed by a variation
// selector -- U+FE0F is what most keyboards insert and U+FE0E is its text-presentation
// twin. Both are accepted, because the three forms are indistinguishable on nearly every
// screen and which one a person gets depends on their keyboard. Rejecting one would make
// a badge that looks correct everywhere fail for a reason nobody can see.
constexpr char kTriggerNameMark[] = "\xF0\x9F\x9B\xB0";   // U+1F6F0
constexpr char kVariationSelector16[] = "\xEF\xB8\x8F";   // U+FE0F
constexpr char kVariationSelector15[] = "\xEF\xB8\x8E";   // U+FE0E

// The conspicuous marker. Matched case-insensitively so a capitalized `0X4` still works:
// a slip on the shift key should not be a silent failure.
constexpr char kTriggerDecoy[] = "0x4";

// Which letter of the name the Morse is built from. 1-based, so 4 means the fourth.
constexpr size_t kTriggerLetterIndex = 4;

namespace detail {

inline size_t cstrLen(const char* s) {
  size_t n = 0;
  while (s != nullptr && s[n] != '\0') n++;
  return n;
}

inline bool endsWithBytes(const char* s, size_t len, const char* suffix, size_t suffix_len) {
  if (s == nullptr || suffix_len == 0 || len < suffix_len) return false;
  for (size_t i = 0; i < suffix_len; i++) {
    if (s[len - suffix_len + i] != suffix[i]) return false;
  }
  return true;
}

inline char upperAscii(char c) {
  return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

inline bool isAsciiLetter(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

}  // namespace detail

// How long `name` is with its trailing mark removed, including any variation selector.
// A name with no mark comes back unchanged, so this is safe to call unconditionally.
inline size_t nameLenWithoutMark(const char* name, size_t len) {
  if (name == nullptr) return 0;
  const size_t vs = 3, mark = 4;
  if (detail::endsWithBytes(name, len, kVariationSelector16, vs) ||
      detail::endsWithBytes(name, len, kVariationSelector15, vs)) {
    len -= vs;
  }
  if (detail::endsWithBytes(name, len, kTriggerNameMark, mark)) len -= mark;
  return len;
}

// Does this sender's name carry the mark, at the END and nowhere else?
//
// At the end specifically. A mark in the middle of a name does not count, so a name that
// merely CONTAINS the character cannot trigger -- which keeps the rule something a person
// can check by looking at the last glyph rather than hunting through a string.
inline bool nameHasMark(const char* name, size_t len) {
  if (name == nullptr) return false;
  return nameLenWithoutMark(name, len) != len;
}

// The nth ASCII letter of `name`, uppercased, ignoring digits, punctuation, spaces and
// anything multi-byte. Returns 0 when the name holds fewer than n letters.
//
// LETTERS ONLY, not letters-and-digits, and that is what keeps this cheap: `morseFor()`
// covers A-Z and returns nullptr for everything else, so a letters-only rule needs no
// change to the Morse table and inherits its existing tests. It does mean a name must
// carry at least n letters before the mark to be able to trigger at all.
inline char nthLetter(const char* name, size_t len, size_t n) {
  if (name == nullptr || n == 0) return 0;
  size_t seen = 0;
  for (size_t i = 0; i < len; i++) {
    if (!detail::isAsciiLetter(name[i])) continue;
    if (++seen == n) return detail::upperAscii(name[i]);
  }
  return 0;
}

// The letter this sender's Morse is built from: the 4th letter of the name, with the
// trailing mark removed first so the mark can never be mistaken for name content.
inline char triggerLetterFor(const char* name) {
  const size_t len = nameLenWithoutMark(name, detail::cstrLen(name));
  return nthLetter(name, len, kTriggerLetterIndex);
}

// Does `body` contain the conspicuous marker anywhere, case-insensitively?
inline bool bodyHasDecoy(const char* body) {
  if (body == nullptr) return false;
  const size_t need = detail::cstrLen(kTriggerDecoy);
  const size_t len = detail::cstrLen(body);
  if (len < need) return false;
  for (size_t i = 0; i + need <= len; i++) {
    size_t j = 0;
    while (j < need && detail::upperAscii(body[i + j]) == detail::upperAscii(kTriggerDecoy[j])) j++;
    if (j == need) return true;
  }
  return false;
}

// Does `body` end with the Morse for `letter`?
//
// TWO THINGS ARE NORMALIZED, both because the alternative is a silent failure.
//
// EN AND EM DASHES BECOME TWO HYPHENS. Phone keyboards turn `--` into a single dash
// character as you type -- iOS Smart Punctuation and Gboard both do it, both on by
// default -- so `-.--` arrives as `-.` plus one dash. Note the arithmetic: ONE dash
// character stands in for TWO hyphens, so mapping it to a single `-` would turn `-.--`
// into `-.-` and match the wrong letter. It maps to `--`.
//
// TRAILING WHITESPACE IS IGNORED. The sender's client trims it today, so in practice the
// Morse is already flush at the end; tolerating it anyway costs nothing and removes a
// failure that is invisible on screen. Anything else after the Morse is still a refusal.
inline bool bodyEndsWithMorseFor(const char* body, char letter) {
  const char* code = morseFor(letter);
  if (body == nullptr || code == nullptr) return false;

  size_t len = detail::cstrLen(body);
  while (len > 0 && (body[len - 1] == ' ' || body[len - 1] == '\t' ||
                     body[len - 1] == '\r' || body[len - 1] == '\n')) {
    len--;
  }

  // Only the tail can decide this. The longest Morse for a letter is 4 elements, and a
  // dash written as en/em arrives as 3 bytes, so 24 bytes is far more than enough.
  const size_t kWindow = 24;
  size_t start = (len > kWindow) ? len - kWindow : 0;
  // Never start mid-character, or a dash straddling the window edge would not be seen.
  while (start < len && ((uint8_t)body[start] & 0xC0) == 0x80) start++;

  char buf[kWindow * 2 + 1];
  size_t w = 0;
  for (size_t i = start; i < len && w + 2 < sizeof(buf);) {
    const bool is_dash =
        (i + 3 <= len) && (uint8_t)body[i] == 0xE2 && (uint8_t)body[i + 1] == 0x80 &&
        ((uint8_t)body[i + 2] == 0x93 || (uint8_t)body[i + 2] == 0x94);  // U+2013 / U+2014
    if (is_dash) {
      buf[w++] = '-';
      buf[w++] = '-';
      i += 3;
    } else {
      buf[w++] = body[i++];
    }
  }
  buf[w] = '\0';

  return detail::endsWithBytes(buf, w, code, detail::cstrLen(code));
}

// The whole rule, for a message the caller has already decided is in scope.
//
// `sender_name` is the sender's name as it arrived -- for a channel message that is the
// prefix before ": " on the wire, NOT the badge store's copy of it. The store caps a
// sender at 15 bytes and the mark is 4 of them before any variation selector, so a long
// name keeps its mark on the air and loses it in the store: checking the stored copy works
// for short names and fails silently for long ones.
inline bool messageFiresTrigger(const char* sender_name, const char* body) {
  if (sender_name == nullptr || body == nullptr) return false;
  if (!nameHasMark(sender_name, detail::cstrLen(sender_name))) return false;
  if (!bodyHasDecoy(body)) return false;
  const char letter = triggerLetterFor(sender_name);
  if (letter == 0) return false;
  return bodyEndsWithMorseFor(body, letter);
}

// The same rule applied to a CHANNEL message exactly as it arrives on the wire:
// "<name>: <body>".
//
// This exists so the caller cannot reach for the wrong name. The badge store keeps a
// sender truncated to 15 bytes (BadgeStore::kSenderLen is 16) and the mark is 4 bytes
// before any variation selector, so a name of 9 bytes or more carries its mark on the air
// and loses it in the store. A store-based check therefore works for short names and
// fails silently for long ones -- the worst failure this feature could have, since
// nothing on any screen would look wrong. Splitting here, from the wire text, makes that
// mistake unavailable rather than merely discouraged.
//
// The split rule matches offband::splitSender() in helpers/BadgeStore.h: the FIRST ": ",
// and only within the first 31 bytes, because a MeshCore name is at most 31. Text with no
// such separator has no sender and cannot fire.
inline bool messageFiresTriggerOnChannelText(const char* wire_text) {
  if (wire_text == nullptr) return false;

  const size_t len = detail::cstrLen(wire_text);
  size_t sep = 0;
  bool found = false;
  for (size_t i = 0; i + 1 < len && i <= 31; i++) {
    if (wire_text[i] == ':' && wire_text[i + 1] == ' ') { sep = i; found = true; break; }
  }
  if (!found || sep == 0) return false;

  // A MeshCore name is at most 31 bytes; the buffer is generous and the length is checked
  // above, so this cannot truncate the way the store's copy does.
  char name[40];
  if (sep >= sizeof(name)) return false;
  for (size_t i = 0; i < sep; i++) name[i] = wire_text[i];
  name[sep] = '\0';

  return messageFiresTrigger(name, wire_text + sep + 2);
}

}  // namespace offband
