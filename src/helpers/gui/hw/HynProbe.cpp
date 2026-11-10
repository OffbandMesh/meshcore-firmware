#include "HynProbe.h"

#include <Arduino.h>
#include <string.h>

#include "HynBus.h"

using offband::gui::HynFamily;

namespace {

void keepRaw(const uint8_t* buf, size_t len, HynProbeResult* r) {
  memcpy(r->raw, buf, len < sizeof(r->raw) ? len : sizeof(r->raw));
}

void probe66xx(TwoWire& w, HynProbeResult* r) {
  uint8_t buf[offband::gui::kCst66xxInfoLen] = {};
  r->valid = hyn66xxReadInfo(w, buf, &r->info, &r->read_ok);
  keepRaw(buf, sizeof(buf), r);
}

void probe3xx(TwoWire& w, HynProbeResult* r) {
  uint8_t buf[offband::gui::kCst3xxInfoLen] = {};
  hynSendReg(w, 0xD101, 2);   // into the info mode
  delay(1);
  r->read_ok = hynReadReg(w, 0xD1F4, 2, buf, sizeof(buf));
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
