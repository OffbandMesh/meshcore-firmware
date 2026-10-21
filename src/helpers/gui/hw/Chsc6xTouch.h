#pragma once

// Our own CHSC6x touch driver (#1442). Heltec's chsc6x library carries no
// license, so nothing is taken from it; the register layout is the chip's
// (see TouchFrame.h). The V4 TFT wires no interrupt line, so the driver is
// polled at LVGL's input-read period.

#include <stdint.h>

#include <Wire.h>

#include "../TouchFrame.h"

class Chsc6xTouch : public offband::gui::TouchSource {
public:
  explicit Chsc6xTouch(TwoWire& wire = Wire1) : _wire(wire) {}

  // Resets the controller, starts the bus and checks the chip answers.
  // Returns false if it doesn't; read() then always reports no touch.
  bool begin(int sda, int scl, int rst, uint8_t addr = 0x2E);

  bool present() const { return _present; }

  // Reads one frame. Returns false on a bus error or a glitch frame.
  bool read(offband::gui::TouchPoint* p) override;

private:
  TwoWire& _wire;
  uint8_t _addr = 0x2E;
  bool _present = false;
};
