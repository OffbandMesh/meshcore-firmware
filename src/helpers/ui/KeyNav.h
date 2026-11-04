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

// #1989: what SW1 does on a main list screen (Messages, Contacts, Nearby).
//
// With a keyboard fitted, a tap moves along the screen cycle, the arrow keys move the
// highlight, and a long press opens the highlighted row.
//
// With NO keyboard fitted the button is all there is, and the focus is in one of two
// places. On the screens, a tap or double tap moves to the next or previous screen, and a
// long press enters the list at its top row. In the list, a tap or double tap moves the
// highlight down or up, a long press opens the row, and a double tap on the top row
// returns to the screens. A tap on the last row stays there.
//
// This replaces #1884, where a tap walked the whole list before the next screen: reaching
// Status meant stepping through every channel and every contact first.
enum class ListTap : uint8_t { None, Select, Open, EnterList, LeaveList, CycleNext, CyclePrev };

struct ListTapResult {
  ListTap action;
  int sel;   // the row to highlight; meaningful only for Select and EnterList
};

inline ListTapResult listTap(uint8_t key, bool keyboard_fitted, bool in_list, int sel, int count) {
  if (key != KEY_NEXT && key != KEY_PREV && key != KEY_ENTER) return {ListTap::None, sel};
  if (keyboard_fitted || !in_list) {
    if (key == KEY_NEXT) return {ListTap::CycleNext, sel};
    if (key == KEY_PREV) return {ListTap::CyclePrev, sel};
    if (count == 0) return {ListTap::None, sel};
    return keyboard_fitted ? ListTapResult{ListTap::Open, sel} : ListTapResult{ListTap::EnterList, 0};
  }
  // A list that emptied while it had the focus hands the focus back to the screens.
  if (count == 0) return {ListTap::LeaveList, sel};
  // A list that shrank under a stale selection steps back onto its last row.
  const int from = sel >= count ? count : (sel < 0 ? 0 : sel);
  if (key == KEY_NEXT) return {ListTap::Select, from + 1 < count ? from + 1 : count - 1};
  if (key == KEY_PREV) return from > 0 ? ListTapResult{ListTap::Select, from - 1} : ListTapResult{ListTap::LeaveList, sel};
  return {ListTap::Open, from < count ? from : count - 1};
}

}  // namespace keynav
