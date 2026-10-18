#pragma once

// #1597: Morse on an LED, as a pure timeline.
//
// The pyramid badge's three front LEDs are on P0.15, and until now the Arduino core owned it
// -- it toggles `LED_BLUE` from a FreeRTOS timer whenever BLE advertises. That is why the
// LEDs kept flashing through #1596's all-dark probe phase, and why suppressing our own LED
// code changed nothing. With `Bluefruit.autoConnLed(false)` the pin becomes ours, and this
// decides what goes on it.
//
// WHY MORSE IS MORE THAN A JOKE. Driven from the MAIN LOOP rather than a timer, the pattern
// stops mid-message when the loop wedges -- which is exactly the #275 liveness property.
// That matters because the pyramid badge has two LED groups and three things wanting them:
// BLE status, messages, and liveness. One pattern carrying BLE-advertising status AND
// liveness together takes three consumers down to two.
//
// A TIMELINE, not a state machine. Given the text and a unit index, this says lit or dark.
// No stored position, nothing to resynchronise, and the whole message is addressable -- so
// a test can walk all 129 units of it in a loop rather than simulating ten seconds.

#include <stdint.h>

namespace offband {

// Standard Morse, in units: dot 1, dash 3, gap between elements 1, between letters 3,
// between words 7.
constexpr uint32_t kMorseDot = 1;
constexpr uint32_t kMorseDash = 3;
constexpr uint32_t kMorseElementGap = 1;
constexpr uint32_t kMorseLetterGap = 3;
constexpr uint32_t kMorseWordGap = 7;

// The owner's choice: slow enough to decode on sight, fast enough that the loop does not
// drag. At 80 ms, "I AM NOT WORTHY" is 129 units = 10.3 s per repetition.
constexpr uint32_t kMorseUnitMs = 80;

// A-Z only. Returns nullptr for anything else, which the walker skips -- so punctuation or
// a lower-case letter costs nothing rather than producing garbage.
inline const char* morseFor(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  switch (c) {
    case 'A': return ".-";    case 'B': return "-...";  case 'C': return "-.-.";
    case 'D': return "-..";   case 'E': return ".";     case 'F': return "..-.";
    case 'G': return "--.";   case 'H': return "....";  case 'I': return "..";
    case 'J': return ".---";  case 'K': return "-.-";   case 'L': return ".-..";
    case 'M': return "--";    case 'N': return "-.";    case 'O': return "---";
    case 'P': return ".--.";  case 'Q': return "--.-";  case 'R': return ".-.";
    case 'S': return "...";   case 'T': return "-";     case 'U': return "..-";
    case 'V': return "...-";  case 'W': return ".--";   case 'X': return "-..-";
    case 'Y': return "-.--";  case 'Z': return "--..";
    default: return nullptr;
  }
}

// What the message looks like at one instant: how long the whole thing is, and whether the
// LED is lit at that unit.
//
// ONE function answering both, and returning both, on purpose. The gap rules are the only
// thing in here that is easy to get subtly wrong, and a separate length function would be a
// second copy of them to drift. Review called an earlier version of this a trap, and it was
// right about the SHAPE of that version -- it selected between the two jobs with a sentinel
// `want` and a `bool*` out-parameter, which is exactly the kind of cleverness a maintainer
// breaks. Returning a small struct keeps the single walker and loses the sentinel.
struct MorseAt {
  uint32_t total_units;
  bool lit;
};

inline MorseAt morseAt(const char* text, uint32_t want) {
  MorseAt r{0, false};
  bool* lit = &r.lit;
  if (text == nullptr) return r;

  uint32_t pos = 0;
  bool first_word = true;
  size_t i = 0;
  while (text[i] != '\0') {
    if (text[i] == ' ') { i++; continue; }         // word boundaries handled below
    if (!first_word) pos += kMorseWordGap;
    first_word = false;

    bool first_letter = true;
    // One word: letters until a space or the end.
    while (text[i] != '\0' && text[i] != ' ') {
      const char* code = morseFor(text[i]);
      i++;
      if (code == nullptr) continue;               // not encodable: contributes nothing
      if (!first_letter) pos += kMorseLetterGap;
      first_letter = false;

      for (size_t e = 0; code[e] != '\0'; e++) {
        if (e > 0) pos += kMorseElementGap;
        const uint32_t len = (code[e] == '.') ? kMorseDot : kMorseDash;
        if (want >= pos && want < pos + len) *lit = true;
        pos += len;
      }
    }
  }
  r.total_units = pos;
  return r;
}

inline uint32_t morseTotalUnits(const char* text) { return morseAt(text, 0).total_units; }

// Which unit of the message `elapsed_ms` lands on, given the message length. Separate from
// the walk so a caller can notice the unit has NOT changed and skip the walk entirely --
// see the caller note below.
inline uint32_t morseUnitAt(uint32_t elapsed_ms, uint32_t total_units,
                            uint32_t unit_ms = kMorseUnitMs) {
  if (total_units == 0 || unit_ms == 0) return 0;
  return (elapsed_ms / unit_ms) % total_units;
}

// Is the LED lit, `elapsed_ms` into a repeating message?
//
// CALLERS IN A HOT LOOP SHOULD NOT USE THIS. It walks the message every call, and the main
// loop runs thousands of times per 80 ms unit -- so this would re-derive the same answer
// over and over on a battery-powered badge. Review flagged that as the highest-severity
// item and was right. The loop instead caches the total once, calls morseUnitAt(), and only
// walks when the unit has actually changed: one walk per 80 ms rather than per iteration.
//
// This stays because it is what the tests drive, where clarity matters more than cycles.
inline bool morseLitAt(const char* text, uint32_t elapsed_ms, uint32_t unit_ms = kMorseUnitMs) {
  const uint32_t total = morseTotalUnits(text);
  if (total == 0 || unit_ms == 0) return false;
  return morseAt(text, morseUnitAt(elapsed_ms, total, unit_ms)).lit;
}

}  // namespace offband
