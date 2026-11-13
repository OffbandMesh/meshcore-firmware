#pragma once

// The T-Deck Pro keyboard (Epic C #2098, C1 #2149): a TI TCA8418 at 0x34 on
// the board I2C bus, scanning the 4x10 key matrix, with its interrupt line
// on PIN_KEYBOARD_INT and the key backlight on PIN_KEYBOARD_LED. Our own
// code, from the datasheet (TI SCPS215G); what the keys type is in the pure
// layer, ../TdeckProKeys.h.
//
// The same shape as TDeckKeyboard, so the GUI's key path takes either.

#include <stdint.h>

#include <Wire.h>

#include "../TdeckProKeys.h"

class Tca8418Keyboard {
public:
  // int_pin and led_pin may be -1 for none. Without an interrupt line the
  // chip is polled every kSafetyPollMs.
  Tca8418Keyboard(TwoWire& wire, int int_pin, int led_pin) : _wire(wire), _int_pin(int_pin), _led_pin(led_pin) {}

  // Finds the chip, puts the 4x10 matrix in key-scan mode, empties its FIFO.
  bool begin();
  bool present() const { return _present; }

  // Takes what the chip's FIFO holds into a queue. Cheap when the interrupt
  // line is idle, so the panel's BUSY wait can call it on every pass.
  void service();

  // The next key byte, or 0 when none is waiting. *repeat says it is a held
  // Backspace's repeat rather than a press.
  uint8_t read(bool* repeat = nullptr);

  // The keys' backlight: lit for any duty above 0. The LED is on a plain
  // GPIO, so there's no brightness.
  bool setBacklight(uint8_t duty);

  struct Stats {
    uint32_t events;       // FIFO bytes taken
    uint32_t overflows;    // the chip's FIFO filled and lost an event
    uint32_t dropped;      // our own queue was full
    uint32_t bus_errors;
    uint32_t int_missed;   // the safety poll found events with INT idle
  };
  Stats takeStats();

  // The safety poll, for an interrupt line that never goes low.
  static constexpr uint32_t kSafetyPollMs = 100;

private:
  bool readReg(uint8_t reg, uint8_t* v);
  bool writeReg(uint8_t reg, uint8_t v);
  void busError();

  TwoWire& _wire;
  int _int_pin, _led_pin;
  bool _present = false;
  bool _overflow_pending = false;
  uint8_t _bus_fail_run = 0;
  uint32_t _last_poll = 0;
  offband::gui::ProKeyQueue _queue;
  offband::gui::ProKeyState _state;
  Stats _stats = {};
  uint32_t _dropped_seen = 0;
};
