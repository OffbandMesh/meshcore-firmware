#pragma once

#include <stdint.h>
#include "UIScreen.h"

// Navigation shared by the button, the joystick and a keyboard. Header-only so it is
// unit-tested natively. Keys arrive through handleInput(char), and several key codes
// are above 127, so callers pass them through uint8_t.
namespace keynav {

enum class Step : uint8_t { None, Prev, Next };

// Which way a key moves a paged screen: Left, Up and Prev go back; Right, Down and
// Next go on.
inline Step pageStep(uint8_t key) {
  switch (key) {
    case KEY_LEFT:
    case KEY_UP:
    case KEY_PREV:  return Step::Prev;
    case KEY_RIGHT:
    case KEY_DOWN:
    case KEY_NEXT:  return Step::Next;
  }
  return Step::None;
}

// The page after one step on a carousel of `count` pages, wrapping at both ends.
inline int stepPage(int page, int count, Step step) {
  if (count <= 0) return 0;
  if (step == Step::Prev) return (page + count - 1) % count;
  if (step == Step::Next) return (page + 1) % count;
  return page;
}

// Whether a key that the current screen did not take should return the UI to Home.
inline bool backsOut(uint8_t key) { return key == KEY_CANCEL; }

// #1590: what a triple-click on SW1 means, given where the UI is.
//
// A badge with no keyboard FITTED has no Esc, and KEY_CANCEL is the only key backsOut()
// accepts -- so on such a unit there was NO way out of Settings at all.
//
// Note it is a RUNTIME absence, not a compile-time one: the badge env sets UI_HAS_CARDKB=1
// and one image serves both badge kinds, probing for the keyboard at boot (#1205). So the
// Settings screens are all compiled in and reachable, and only the key that leaves them
// is missing. That is why this was invisible until a keyboard-less unit got a display.
//
// All four SW1 gestures were already spoken for (click, double, long, and triple toggling
// the buzzer), so the way out has to share one. Triple-click backs out where there IS
// somewhere to back out to, and keeps its original meaning at Home, where there is not.
// The two can never compete: "get me out of here" is meaningless on the Home screen.
//
// Returns 0 for "no key -- do the gesture's other job".
inline uint8_t tripleClickKey(bool at_home) {
  return at_home ? (uint8_t)0 : (uint8_t)KEY_CANCEL;
}

}  // namespace keynav
