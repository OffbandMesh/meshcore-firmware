#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "CardKbKeys.h"

// Polls an M5Stack CardKB-compatible keyboard over I2C (see CardKbKeys.h for the codes).
// The keyboard is optional. When it is absent the badge stays a button-driven device, and
// the only cost is one address probe every few seconds, which also picks up a keyboard
// plugged in after boot.
class CardKbInput {
  TwoWire* _wire = nullptr;
  bool _present = false;
  unsigned long _next_poll = 0;
  unsigned long _next_probe = 0;

  bool probe();

public:
  static constexpr unsigned long kPollMillis = 20;     // 50 Hz; the keyboard holds one key
  static constexpr unsigned long kReprobeMillis = 2000;

  // Call once the bus is up. Returns whether a keyboard answered.
  bool begin(TwoWire& wire);
  bool isPresent() const { return _present; }

  // The raw code of a key pressed since the last read, or 0. Reads at most once per
  // kPollMillis. A read that fails marks the keyboard absent until a probe finds it again.
  uint8_t poll(unsigned long now);
};
