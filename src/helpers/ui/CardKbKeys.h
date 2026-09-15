#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "UIScreen.h"

// M5Stack CardKB, and keyboards built to its protocol such as the QCC badge's. Key codes
// are from M5's keyboard firmware (m5stack/M5-ProductExampleCodes, Unit/CARDKB/
// firmware_328p). Its sticky Shift ("Aa"), Sym and Fn keys are applied inside the
// keyboard, which never sends a code for a modifier, so the host only ever sees the
// resulting key. Header-only and free of Arduino, so the decode is unit-tested natively.
namespace cardkb {

constexpr uint8_t kAddress = 0x5F;

// What the keyboard sends.
constexpr uint8_t kDel = 8, kTab = 9, kEnter = 13, kEsc = 27, kSpace = 32, kShiftDel = 127;
constexpr uint8_t kLeft = 180, kUp = 181, kDown = 182, kRight = 183;
constexpr uint8_t kFnFirst = 128, kFnLast = 175;

static_assert(kLeft == KEY_LEFT && kUp == KEY_UP && kDown == KEY_DOWN && kRight == KEY_RIGHT,
              "the CardKB's arrow codes are the UI's arrow keys");

// The normal-layer code of each key, in the order of M5's key map. The Fn layer sends
// 128 + a key's index here, which is how an Fn code is traced back to its key. 0 marks
// the map's one empty slot.
constexpr uint8_t kNormalLayer[48] = {
  kEsc,  '1',    '2', '3', '4', '5', '6', '7', '8', '9', '0', kDel,
  kTab,  'q',    'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0,
  kLeft, kUp,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', kEnter,
  kDown, kRight, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', kSpace,
};

// #1233: the key an Fn-layer code came from ('s' for Fn+S), or 0 for any other byte.
inline uint8_t fnBase(uint8_t raw) {
  return (raw >= kFnFirst && raw <= kFnLast) ? kNormalLayer[raw - kFnFirst] : 0;
}

// The key the UI should receive for one byte read from the keyboard, or 0 for none.
// 0 is an idle read and 0xFF a bus with nothing on it. The Fn layer is dropped until a
// screen has a use for it.
inline uint8_t toUiKey(uint8_t raw) {
  switch (raw) {
    case kDel:
    case kShiftDel: return KEY_BACKSPACE;
    case kTab:      return KEY_TAB;
    case kEnter:    return KEY_ENTER;
    case kEsc:      return KEY_CANCEL;
    case kLeft:
    case kUp:
    case kDown:
    case kRight:    return raw;
  }
  return (raw >= 0x20 && raw <= 0x7E) ? raw : 0;
}

// A short name for a raw code, for the diag key-test screen: "ESC", "'a'", "FN+1".
inline void keyName(uint8_t raw, char* out, size_t n) {
  if (n == 0) return;
  const char* s = nullptr;
  switch (raw) {
    case 0:         s = ""; break;
    case kDel:
    case kShiftDel: s = "DEL"; break;
    case kTab:      s = "TAB"; break;
    case kEnter:    s = "ENTER"; break;
    case kEsc:      s = "ESC"; break;
    case kSpace:    s = "SPACE"; break;
    case kLeft:     s = "LEFT"; break;
    case kUp:       s = "UP"; break;
    case kDown:     s = "DOWN"; break;
    case kRight:    s = "RIGHT"; break;
  }
  if (s) {
    snprintf(out, n, "%s", s);
  } else if (raw >= kFnFirst && raw <= kFnLast && kNormalLayer[raw - kFnFirst] != 0) {
    const uint8_t base = kNormalLayer[raw - kFnFirst];
    if (base > 0x20 && base <= 0x7E) {
      snprintf(out, n, "FN+%c", (char)base);
    } else {
      char base_name[8];
      keyName(base, base_name, sizeof base_name);
      snprintf(out, n, "FN+%s", base_name);
    }
  } else if (raw > 0x20 && raw <= 0x7E) {
    snprintf(out, n, "'%c'", (char)raw);
  } else {
    snprintf(out, n, "?");
  }
}

}  // namespace cardkb
