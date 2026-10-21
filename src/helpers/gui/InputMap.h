#pragma once

// The GUI input map (design spec v0.3 §"Input", D1 #1323 §1.8): buttons,
// touch and keyboard all reduce to the same small set of actions, so every
// screen handles actions, never raw hardware events.
//
// USR (the board's user button) works as on stock MeshCore: click next,
// double-click previous, hold select, triple-click buzzer. A 3 s hold is the
// pocket lock. The second button (AUX, GPIO35 on the V4 TFT kit) scrolls up
// and goes back (#1445 bench, owner).

#include <stdint.h>

namespace offband {
namespace gui {

// A press shorter than kHoldMs is a click; kHoldMs or longer is a hold;
// kLongHoldMs or longer is a long hold. kHoldMs matches stock MeshCore
// (ui-new LONG_PRESS_MILLIS). kMultiClickMs is the gap allowed between
// releasing one click and pressing the next; stock's 280 ms proved too tight
// on the bench (#1445).
constexpr uint32_t kHoldMs = 1200;
constexpr uint32_t kLongHoldMs = 3000;
constexpr uint32_t kMultiClickMs = 400;

enum class PressKind : uint8_t { Press, Hold, LongHold };
PressKind classifyPress(uint32_t held_ms);

enum class InputEvent : uint8_t {
  UsrClick, UsrDoubleClick, UsrTripleClick, UsrHold, UsrLongHold,
  AuxPress, AuxHold, AuxLongHold,
  TouchTap, TouchSwipeRight,
  KeyTab, KeyEnter, KeyEsc, KeyAltDigit, KeyOther,
  // The T-Deck (J4 #1727): its keyboard has no Esc, so Backspace goes back
  // outside a text field. The trackball's press is USR (GPIO0).
  KeyBackspace,
  TrackUp, TrackDown, TrackLeft, TrackRight,
};

enum class Action : uint8_t {
  None,
  Next,          // next item / scroll down
  Prev,          // previous item / scroll up
  Open,          // open / select
  Back,
  Canned,        // canned replies
  Wake,
  PocketLock,    // screen off, and only USR wakes it
  ToggleBuzzer,
};

// With the screen off, an event only wakes it. While pocket-locked, only USR
// wakes it and every other event is ignored.
Action mapInput(InputEvent ev, bool screen_on, bool pocket_locked);

}  // namespace gui
}  // namespace offband
