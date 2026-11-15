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

#include <stddef.h>
#include <stdint.h>

namespace offband {
namespace gui {

constexpr uint8_t kTDeckKeyboardAddr = 0x55;

enum class KeyKind : uint8_t { None, Char, Enter, Backspace };

// What one keyboard byte is. Printable ASCII is a character; anything else
// that isn't Enter or Backspace (Shift on a symbol gives a control code,
// Alt+C gives 0x0C) is ignored.
KeyKind classifyKey(uint8_t b);

// The keyboard's backlight follows the screen: lit while the screen is on,
// off once it dims or goes dark (tester bench, #1730). 127 is the keyboard
// firmware's own Alt+B brightness.
constexpr uint8_t kKeyboardBacklightOn = 127;
uint8_t keyboardBacklightFor(bool screen_on);

// The bytes that set it. LilyGo's keyboard firmware handles 0x01 (brightness)
// and then falls through into 0x02 (Alt+B's default brightness), reading a
// second byte, so the brightness goes twice: one write sets both, and Alt+B
// afterwards toggles to the same level.
constexpr size_t kKeyboardBacklightCmdLen = 3;
void keyboardBacklightCommand(uint8_t duty, uint8_t out[kKeyboardBacklightCmdLen]);

// The canned replies a digit picks in an open conversation (owner,
// 2026-10-21: the keyboard never sends Alt, so Alt+digit can't be the
// shortcut). 1-4 are the four replies; anything else is -1.
constexpr int kCannedReplies = 4;
int cannedSlotForKey(uint8_t b);

// The text field a keyboard key would go to.
enum class FieldKind : uint8_t {
  None,      // no field open: keys are navigation
  Message,   // a message draft
  Other,     // a name, a search, a log-in, a channel step
};

// Whether the open field takes a keyboard key: Enter and characters always.
// Backspace too, except on an empty field other than a message, where it is
// left to navigation as Back. A message never gives it up, so a quick delete
// that runs past the start stays in the draft (owner, #2166). Other fields
// keep Back: on a unit without touch it is their only way out.
bool fieldTakesKey(KeyKind k, FieldKind field, bool empty);

// The start of a sentence: an empty text, or "." "!" "?" then a space. The
// on-screen keyboard's automatic capital and the keyboard's sentenceCase()
// share it.
bool sentenceStart(const char* text, size_t len);

// The case of a typed character for a keyboard that reports its Shift
// (the T-Deck Pro, #2167). At a sentence start in `text` a letter becomes
// a capital, or lowercase when a tapped Shift came with it, as a tap cancels
// a phone's automatic capital. Elsewhere, and for anything not a letter, it
// comes back as typed.
char sentenceCase(char ch, bool tapped_shift, const char* text, size_t len);

// The most steps one direction moves per poll, so a hard spin moves focus a
// few rows rather than racing to the end of a list.
constexpr uint8_t kTrackballMaxSteps = 3;

// Turns the pulses counted since the last poll into steps, `per_step`
// pulses each. A partial step's pulses carry over in *carry; beyond
// kTrackballMaxSteps the rest are dropped.
uint8_t trackballSteps(uint32_t pulses, uint8_t per_step, uint8_t* carry);

}  // namespace gui
}  // namespace offband
