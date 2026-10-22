#include "Gt911Touch.h"

#include <Arduino.h>

using offband::gui::TouchPoint;

bool Gt911Touch::begin(int sda, int scl, int rst) {
  (void)rst;
  // The bus is shared with the keyboard. If the board already started it,
  // this does nothing; if not, it starts at the core's default 100 kHz rather
  // than pushing every device on it to a faster clock.
  _wire.begin(sda, scl);
  const uint8_t addrs[2] = {0x5D, 0x14};
  for (uint8_t i = 0; i < 2; i++) {
    Probe& pr = _probe[i];
    pr = {};
    _wire.beginTransmission(addrs[i]);
    pr.acked = _wire.endTransmission() == 0;
    if (!pr.acked) continue;
    _addr = addrs[i];
    pr.id_read = readReg(offband::gui::kGt911ProductId, pr.id, sizeof pr.id);
    if (pr.id_read && offband::gui::isGt911ProductId(pr.id, sizeof pr.id)) {
      _present = true;
      break;
    }
  }
  return _present;
}

bool Gt911Touch::readReg(uint16_t reg, uint8_t* buf, uint8_t len) {
  _wire.beginTransmission(_addr);
  _wire.write(uint8_t(reg >> 8));
  _wire.write(uint8_t(reg & 0xFF));
  if (_wire.endTransmission(false) != 0) return false;
  if (_wire.requestFrom(_addr, len) != len) return false;
  return _wire.readBytes(buf, len) == len;
}

bool Gt911Touch::writeReg(uint16_t reg, uint8_t value) {
  _wire.beginTransmission(_addr);
  _wire.write(uint8_t(reg >> 8));
  _wire.write(uint8_t(reg & 0xFF));
  _wire.write(value);
  return _wire.endTransmission() == 0;
}

bool Gt911Touch::read(TouchPoint* p) {
  if (!_present || !p) return false;
  _stats.reads++;
  uint8_t status = 0;
  if (!readReg(offband::gui::kGt911Status, &status, 1)) {
    _stats.bus_errors++;
    return false;
  }
  uint8_t point[offband::gui::kGt911PointLen];
  const bool any = (status & 0x80) && (status & 0x0F);
  if (any && !readReg(offband::gui::kGt911Point1, point, sizeof point)) {
    _stats.bus_errors++;
    return false;
  }
  // A report was read: clear the status, or the chip sends no new one.
  if (status & 0x80) {
    _stats.reports++;
    writeReg(offband::gui::kGt911Status, 0);
  }
  if (!_tracker.update(status, any ? point : nullptr, any ? sizeof point : 0, p)) {
    _stats.invalid++;
    return false;
  }
  if (any && p->pressed) {
    _stats.presses++;
    _stats.last_x = p->x;
    _stats.last_y = p->y;
  }
  return true;
}

Gt911Touch::Stats Gt911Touch::takeStats() {
  Stats s = _stats;
  _stats = {};
  _stats.last_x = s.last_x;
  _stats.last_y = s.last_y;
  return s;
}
