#pragma once

// The GUI input map (design spec v0.3 §"Input", D1 #1323 §1.8): buttons,
// touch and keyboard all reduce to the same small set of actions, so every
// screen handles actions, never raw hardware events.

#include <stdint.h>

namespace offband {
namespace gui {

// A press shorter than kHoldMs is a press; kHoldMs or longer is a hold;
// kLongHoldMs or longer is a long hold (sleep).
constexpr uint32_t kHoldMs = 600;
constexpr uint32_t kLongHoldMs = 3000;

enum class PressKind : uint8_t { Press, Hold, LongHold };
PressKind classifyPress(uint32_t held_ms);

enum class InputEvent : uint8_t {
  PrgPress, PrgHold, PrgLongHold,
  UserPress, UserHold,
  TouchTap, TouchSwipeRight,
  KeyTab, KeyEnter, KeyEsc, KeyAltDigit, KeyOther,
};

enum class Action : uint8_t {
  None,
  NextFocus,  // next tab / move focus
  Open,       // open / primary action
  Back,
  Canned,     // canned replies
  Wake,
  Sleep,
};

// With the screen off, every event wakes it and does nothing else.
// With the screen on, a PRG long hold sleeps it.
Action mapInput(InputEvent ev, bool screen_on);

}  // namespace gui
}  // namespace offband
