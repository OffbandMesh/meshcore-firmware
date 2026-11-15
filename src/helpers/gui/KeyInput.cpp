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

bool fieldTakesKey(KeyKind k, FieldKind field, bool empty) {
  if (field == FieldKind::None) return false;
  switch (k) {
    case KeyKind::Char:
    case KeyKind::Enter:
      return true;
    case KeyKind::Backspace:
      return !empty || field == FieldKind::Message;
    default:
      return false;
  }
}

bool sentenceStart(const char* text, size_t len) {
  if (len == 0) return true;
  if (!text || len < 2 || text[len - 1] != ' ') return false;
  const char end = text[len - 2];
  return end == '.' || end == '!' || end == '?';
}

char sentenceCase(char ch, bool tapped_shift, const char* text, size_t len) {
  if (!sentenceStart(text, len)) return ch;
  if (ch >= 'a' && ch <= 'z') return tapped_shift ? ch : char(ch - 'a' + 'A');
  if (ch >= 'A' && ch <= 'Z') return tapped_shift ? char(ch - 'A' + 'a') : ch;
  return ch;
}

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
