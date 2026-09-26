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

}  // namespace keynav
