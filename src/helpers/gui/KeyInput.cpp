#include "KeyInput.h"

namespace offband {
namespace gui {

KeyKind classifyKey(uint8_t b) {
  if (b == 0x0D || b == 0x0A) return KeyKind::Enter;
  if (b == 0x08 || b == 0x7F) return KeyKind::Backspace;
  if (b >= 0x20 && b <= 0x7E) return KeyKind::Char;
  return KeyKind::None;
}

uint8_t keyboardBacklightFor(bool screen_on) { return screen_on ? kKeyboardBacklightOn : 0; }

void keyboardBacklightCommand(uint8_t duty, uint8_t out[kKeyboardBacklightCmdLen]) {
  out[0] = 0x01;
  out[1] = duty;
  out[2] = duty;
}

int cannedSlotForKey(uint8_t b) {
  return b >= '1' && b < '1' + kCannedReplies ? int(b - '1') : -1;
}

uint8_t trackballSteps(uint32_t pulses, uint8_t per_step, uint8_t* carry) {
  if (!carry) return 0;
  if (per_step == 0) per_step = 1;
  const uint32_t total = pulses + *carry;
  const uint32_t steps = total / per_step;
  if (steps >= kTrackballMaxSteps) {
    *carry = 0;   // a spin's leftover would move it again on the next poll
    return kTrackballMaxSteps;
  }
  *carry = uint8_t(total % per_step);
  return uint8_t(steps);
}

}  // namespace gui
}  // namespace offband
