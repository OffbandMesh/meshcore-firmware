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

  // What begin() found, for the diag log: per address, whether it answered
  // and the product id bytes it gave (#1730 bench).
  struct Probe {
    bool acked;
    bool id_read;
    uint8_t id[4];
  };
  const Probe& probe(uint8_t i) const { return _probe[i < 2 ? i : 1]; }

  // Counts since the last take, for the diag log.
  struct Stats {
    uint32_t reads, bus_errors, reports, presses, invalid;
    uint16_t last_x, last_y;   // the last pressed point, raw
  };
  Stats takeStats();

private:
  bool readReg(uint16_t reg, uint8_t* buf, uint8_t len);
  bool writeReg(uint16_t reg, uint8_t value);

  TwoWire& _wire;
  uint8_t _addr = 0x5D;
  bool _present = false;
  offband::gui::Gt911Tracker _tracker;
  Probe _probe[2] = {};
  Stats _stats = {};
};
