#include "Chsc6xTouch.h"

#include <Arduino.h>

bool Chsc6xTouch::begin(int sda, int scl, int rst, uint8_t addr) {
  _addr = addr;
  if (rst >= 0) {
    pinMode(rst, OUTPUT);
    digitalWrite(rst, LOW);
    delay(10);
    digitalWrite(rst, HIGH);
    delay(50);
  }
  _wire.begin(sda, scl, 400000);
  _wire.beginTransmission(_addr);
  _present = _wire.endTransmission() == 0;
  return _present;
}

bool Chsc6xTouch::read(offband::gui::TouchPoint* p) {
  if (!_present || !p) return false;
  _wire.beginTransmission(_addr);
  _wire.write(uint8_t(0x00));
  if (_wire.endTransmission(false) != 0) return false;

  uint8_t buf[offband::gui::kChsc6xFrameLen];
  const size_t want = sizeof(buf);
  if (_wire.requestFrom(_addr, uint8_t(want)) != want) return false;
  if (_wire.readBytes(buf, want) != want) return false;
  return offband::gui::parseChsc6xFrame(buf, want, p);
}
