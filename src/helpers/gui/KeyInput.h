#pragma once

// The T-Deck's keyboard and trackball (Epic J #1434, J4 #1727), as plain
// values the GUI can act on. Arduino-free so the native tests cover it.
//
// The keyboard is an ESP32-C3 at I2C 0x55. Each one-byte read is the next
// key as ASCII, or 0 when none is waiting. Shift and Sym are applied on the
// keyboard itself; Alt is never sent (LilyGo's keyboard firmware, #1724).
//
// Each trackball direction is a line that pulses as the ball rolls. The
// pulses are counted and turned into steps here.

#include <stdint.h>

namespace offband {
namespace gui {

constexpr uint8_t kTDeckKeyboardAddr = 0x55;

enum class KeyKind : uint8_t { None, Char, Enter, Backspace };

// What one keyboard byte is. Printable ASCII is a character; anything else
// that isn't Enter or Backspace (Shift on a symbol gives a control code,
// Alt+C gives 0x0C) is ignored.
KeyKind classifyKey(uint8_t b);

// The canned replies a digit picks in an open conversation (owner,
// 2026-10-21: the keyboard never sends Alt, so Alt+digit can't be the
// shortcut). 1-4 are the four replies; anything else is -1.
constexpr int kCannedReplies = 4;
int cannedSlotForKey(uint8_t b);

// The most steps one direction moves per poll, so a hard spin moves focus a
// few rows rather than racing to the end of a list.
constexpr uint8_t kTrackballMaxSteps = 3;

// Turns the pulses counted since the last poll into steps, `per_step`
// pulses each. A partial step's pulses carry over in *carry; beyond
// kTrackballMaxSteps the rest are dropped.
uint8_t trackballSteps(uint32_t pulses, uint8_t per_step, uint8_t* carry);

}  // namespace gui
}  // namespace offband
