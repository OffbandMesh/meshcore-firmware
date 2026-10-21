#include "TDeckInput.h"

#include <Arduino.h>

#include "../KeyInput.h"

bool TDeckKeyboard::begin() {
  _wire.beginTransmission(offband::gui::kTDeckKeyboardAddr);
  _present = _wire.endTransmission() == 0;
  return _present;
}

uint8_t TDeckKeyboard::read() {
  if (!_present) return 0;
  if (_wire.requestFrom(offband::gui::kTDeckKeyboardAddr, uint8_t(1)) != 1) return 0;
  const int b = _wire.read();
  return b < 0 ? 0 : uint8_t(b);
}

// One counter per direction, added to and taken atomically, so the loop
// takes a count without masking interrupts.
static volatile uint32_t s_pulses[TDeckTrackball::kDirs] = {};

static void IRAM_ATTR onUp() { __atomic_fetch_add(&s_pulses[TDeckTrackball::Up], 1u, __ATOMIC_RELAXED); }
static void IRAM_ATTR onDown() { __atomic_fetch_add(&s_pulses[TDeckTrackball::Down], 1u, __ATOMIC_RELAXED); }
static void IRAM_ATTR onLeft() { __atomic_fetch_add(&s_pulses[TDeckTrackball::Left], 1u, __ATOMIC_RELAXED); }
static void IRAM_ATTR onRight() { __atomic_fetch_add(&s_pulses[TDeckTrackball::Right], 1u, __ATOMIC_RELAXED); }

void TDeckTrackball::begin(int up, int down, int left, int right) {
  const int pins[kDirs] = {up, down, left, right};
  void (*const isr[kDirs])() = {onUp, onDown, onLeft, onRight};
  for (uint8_t i = 0; i < kDirs; i++) {
    if (pins[i] < 0) continue;
    pinMode(pins[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pins[i]), isr[i], FALLING);
  }
}

uint32_t TDeckTrackball::take(Dir d) {
  if (d >= kDirs) return 0;
  return __atomic_exchange_n(&s_pulses[d], 0u, __ATOMIC_RELAXED);
}
