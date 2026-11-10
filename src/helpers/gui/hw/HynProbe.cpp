#include "HynProbe.h"

#include <Arduino.h>
#include <string.h>

using offband::gui::HynFamily;

namespace {

// Low for 10 ms, then 50 ms for the controller to boot.
void hynReset(int rst) {
  if (rst < 0) return;
  pinMode(rst, OUTPUT);
  digitalWrite(rst, LOW);
  delay(10);
  digitalWrite(rst, HIGH);
  delay(50);
}

bool sendReg(TwoWire& w, uint32_t reg, uint8_t reg_len, bool stop) {
  w.beginTransmission(offband::gui::kHynAddr);
  for (int i = reg_len - 1; i >= 0; i--) w.write(uint8_t(reg >> (8 * i)));
  return w.endTransmission(stop) == 0;
}

// Writes the register address, then reads `len` bytes after a repeated start.
bool readReg(TwoWire& w, uint32_t reg, uint8_t reg_len, uint8_t* buf, size_t len) {
  if (!sendReg(w, reg, reg_len, false)) return false;
  if (w.requestFrom(uint16_t(offband::gui::kHynAddr), len, true) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = uint8_t(w.read());
  return true;
}

void keepRaw(const uint8_t* buf, size_t len, HynProbeResult* r) {
  memcpy(r->raw, buf, len < sizeof(r->raw) ? len : sizeof(r->raw));
}

void probe66xx(TwoWire& w, HynProbeResult* r) {
  uint8_t buf[offband::gui::kCst66xxInfoLen] = {};
  // Normal mode: the low-power I2C wake twice, then the mode writes.
  sendReg(w, 0xD0000400, 4, true);
  delay(1);
  sendReg(w, 0xD0000400, 4, true);
  sendReg(w, 0xD0000000, 4, true);
  sendReg(w, 0xD0000C00, 4, true);
  sendReg(w, 0xD0000100, 4, true);
  for (uint8_t attempt = 0; attempt < 4; attempt++) {
    r->read_ok = readReg(w, 0xD0030000, 4, buf, sizeof(buf));
    if (r->read_ok && (r->valid = offband::gui::parseCst66xxInfo(buf, sizeof(buf), &r->info))) break;
    delay(1);
    sendReg(w, 0xD0000400, 4, true);
  }
  keepRaw(buf, sizeof(buf), r);
}

void probe3xx(TwoWire& w, HynProbeResult* r) {
  uint8_t buf[offband::gui::kCst3xxInfoLen] = {};
  sendReg(w, 0xD101, 2, true);   // into the info mode
  delay(1);
  r->read_ok = readReg(w, 0xD1F4, 2, buf, sizeof(buf));
  if (r->read_ok) r->valid = offband::gui::parseCst3xxInfo(buf, sizeof(buf), &r->info);
  keepRaw(buf, sizeof(buf), r);
}

}  // namespace

void hynProbe(TwoWire& wire, int rst, HynFamily family, HynProbeResult* out) {
  *out = HynProbeResult();
  hynReset(rst);
  wire.beginTransmission(offband::gui::kHynAddr);
  out->acked = wire.endTransmission() == 0;
  if (out->acked) {
    if (family == HynFamily::Cst66xx) probe66xx(wire, out);
    else if (family == HynFamily::Cst3xx) probe3xx(wire, out);
  }
  hynReset(rst);
}

bool hynBusAnswers(TwoWire& wire, uint8_t other_addr) {
  wire.beginTransmission(other_addr);
  return wire.endTransmission() == 0;
}
