#include "InputMap.h"

namespace offband {
namespace gui {

PressKind classifyPress(uint32_t held_ms) {
  if (held_ms >= kLongHoldMs) return PressKind::LongHold;
  if (held_ms >= kHoldMs) return PressKind::Hold;
  return PressKind::Press;
}

Action mapInput(InputEvent ev, bool screen_on) {
  if (!screen_on) return Action::Wake;

  switch (ev) {
    case InputEvent::UserPress:
    case InputEvent::KeyTab:
      return Action::NextFocus;
    case InputEvent::PrgPress:
    case InputEvent::TouchTap:
    case InputEvent::KeyEnter:
      return Action::Open;
    case InputEvent::PrgHold:
    case InputEvent::TouchSwipeRight:
    case InputEvent::KeyEsc:
      return Action::Back;
    case InputEvent::UserHold:
    case InputEvent::KeyAltDigit:
      return Action::Canned;
    case InputEvent::PrgLongHold:
      return Action::Sleep;
    case InputEvent::KeyOther:
      return Action::None;
  }
  return Action::None;
}

}  // namespace gui
}  // namespace offband
