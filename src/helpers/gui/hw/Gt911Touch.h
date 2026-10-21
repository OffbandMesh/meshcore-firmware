#pragma once

// Our own GT911 touch driver (Epic J #1434, J3 #1726), for the T-Deck. Built
// from the chip's register map (TouchFrame.h); nothing is taken from another
// library. Polled at LVGL's input period, like the CHSC6x: the T-Deck wires
// INT to GPIO16 but nothing here needs it.
//
// The T-Deck resets the GT911 only at power-on (an RC network, no GPIO;
// LilyGo's schematic, #1724), and the chip latches its I2C address from INT's
// level at that moment, so begin() looks at both 0x5D and 0x14.

#include <stdint.h>

#include <Wire.h>

#include "../TouchFrame.h"

class Gt911Touch : public offband::gui::TouchSource {
public:
  explicit Gt911Touch(TwoWire& wire = Wire) : _wire(wire) {}

  // `rst` is unused (no reset line on the T-Deck); kept so the GUI starts
  // either touch driver the same way.
  bool begin(int sda, int scl, int rst);
  bool present() const { return _present; }
  bool read(offband::gui::TouchPoint* p) override;

private:
  bool readReg(uint16_t reg, uint8_t* buf, uint8_t len);
  bool writeReg(uint16_t reg, uint8_t value);

  TwoWire& _wire;
  uint8_t _addr = 0x5D;
  bool _present = false;
  offband::gui::Gt911Tracker _tracker;
};
