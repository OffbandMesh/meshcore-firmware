#include "HynBus.h"

#include <Arduino.h>

using offband::gui::kHynAddr;

void hynReset(int rst) {
  if (rst < 0) return;
  pinMode(rst, OUTPUT);
  digitalWrite(rst, LOW);
  delay(10);
  digitalWrite(rst, HIGH);
  delay(50);
}

bool hynSendReg(TwoWire& w, uint32_t reg, uint8_t reg_len, bool stop) {
  w.beginTransmission(kHynAddr);
  for (int i = reg_len - 1; i >= 0; i--) w.write(uint8_t(reg >> (8 * i)));
  return w.endTransmission(stop) == 0;
}

bool hynReadMore(TwoWire& w, uint8_t* buf, size_t len) {
  if (w.requestFrom(uint16_t(kHynAddr), len, true) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = uint8_t(w.read());
  return true;
}

bool hynReadReg(TwoWire& w, uint32_t reg, uint8_t reg_len, uint8_t* buf, size_t len) {
  if (!hynSendReg(w, reg, reg_len, false)) return false;
  return hynReadMore(w, buf, len);
}

void hyn66xxNormalMode(TwoWire& w) {
  hynSendReg(w, 0xD0000400, 4);
  delay(1);
  hynSendReg(w, 0xD0000400, 4);
  hynSendReg(w, 0xD0000000, 4);
  hynSendReg(w, 0xD0000C00, 4);
  hynSendReg(w, 0xD0000100, 4);
}

bool hyn66xxReadInfo(TwoWire& w, uint8_t* buf, offband::gui::HynInfo* info, bool* read_ok) {
  hyn66xxNormalMode(w);
  bool ok = false, valid = false;
  for (uint8_t attempt = 0; attempt < 4; attempt++) {
    ok = hynReadReg(w, 0xD0030000, 4, buf, offband::gui::kCst66xxInfoLen);
    if (ok && (valid = offband::gui::parseCst66xxInfo(buf, offband::gui::kCst66xxInfoLen, info))) break;
    delay(1);
    hynSendReg(w, 0xD0000400, 4);
  }
  if (read_ok) *read_ok = ok;
  return valid;
}
