#include "InputMap.h"

namespace offband {
namespace gui {

PressKind classifyPress(uint32_t held_ms) {
  (void)held_ms;
  return PressKind::Press;
}

Action mapInput(InputEvent ev, bool screen_on) {
  (void)ev;
  (void)screen_on;
  return Action::None;
}

}  // namespace gui
}  // namespace offband
