#include "InputMap.h"

namespace offband {
namespace gui {

PressKind classifyPress(uint32_t held_ms) {
  if (held_ms >= kLongHoldMs) return PressKind::LongHold;
  if (held_ms >= kHoldMs) return PressKind::Hold;
  return PressKind::Press;
}

static bool isUsr(InputEvent ev) {
  switch (ev) {
    case InputEvent::UsrClick:
    case InputEvent::UsrDoubleClick:
    case InputEvent::UsrTripleClick:
    case InputEvent::UsrHold:
    case InputEvent::UsrLongHold:
    case InputEvent::TrackPress:
      return true;
    default:
      return false;
  }
}

Action mapInput(InputEvent ev, bool screen_on, bool pocket_locked) {
  if (!screen_on) {
    if (pocket_locked && !isUsr(ev)) return Action::None;
    return Action::Wake;
  }

  switch (ev) {
    case InputEvent::UsrClick:
    case InputEvent::KeyTab:
    case InputEvent::TrackDown:
    case InputEvent::TrackRight:
      return Action::Next;
    case InputEvent::UsrDoubleClick:
    case InputEvent::AuxPress:
    case InputEvent::TrackUp:
      return Action::Prev;
    case InputEvent::TrackLeft:
    case InputEvent::KeyBackspace:
      return Action::Back;
    case InputEvent::UsrHold:
    case InputEvent::TouchTap:
    case InputEvent::KeyEnter:
    case InputEvent::TrackPress:
      return Action::Open;
    case InputEvent::AuxHold:
    case InputEvent::TouchSwipeRight:
    case InputEvent::KeyEsc:
      return Action::Back;
    // A board with AUX doesn't group USR clicks, so the buzzer can't be a
    // triple-click there; it's AUX held 3 s (owner, 2026-10-17, #1534).
    case InputEvent::AuxLongHold:
      return Action::ToggleBuzzer;
    case InputEvent::KeyAltDigit:
      return Action::Canned;
    case InputEvent::UsrTripleClick:
      return Action::ToggleBuzzer;
    case InputEvent::UsrLongHold:
      return Action::PocketLock;
    case InputEvent::KeyOther:
      return Action::None;
  }
  return Action::None;
}

}  // namespace gui
}  // namespace offband
