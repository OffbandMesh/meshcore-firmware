#pragma once

// The T-Deck's keyboard and trackball (Epic J #1434, J4 #1727). Our own
// code, from LilyGo's schematic and keyboard firmware's I2C protocol
// (KeyInput.h); nothing is taken from another library.

#include <stdint.h>

#include <Wire.h>

// The keyboard: an ESP32-C3 on the board I2C bus, polled. It needs the
// peripheral power rail (GPIO10), which the board turns on at boot.
class TDeckKeyboard {
public:
  explicit TDeckKeyboard(TwoWire& wire = Wire) : _wire(wire) {}
  bool begin();
  bool present() const { return _present; }
  // The next key byte, or 0 when none is waiting or the read failed.
  uint8_t read();

private:
  TwoWire& _wire;
  bool _present = false;
};

// The trackball: four direction lines, each pulsing as the ball rolls.
// Pulses are counted in interrupts; the UI loop takes the counts.
class TDeckTrackball {
public:
  enum Dir : uint8_t { Up, Down, Left, Right, kDirs };
  void begin(int up, int down, int left, int right);
  // Pulses on `d` since the last take.
  uint32_t take(Dir d);
};
