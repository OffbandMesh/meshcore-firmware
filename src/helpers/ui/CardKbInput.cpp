#include "CardKbInput.h"

bool CardKbInput::probe() {
  _wire->beginTransmission(cardkb::kAddress);
  _present = (_wire->endTransmission() == 0);
  return _present;
}

bool CardKbInput::begin(TwoWire& wire) {
  _wire = &wire;
  _next_poll = 0;
  _next_probe = 0;
  return probe();
}

uint8_t CardKbInput::poll(unsigned long now) {
  if (_wire == nullptr) return 0;

  if (!_present) {
    if ((long)(now - _next_probe) < 0) return 0;
    _next_probe = now + kReprobeMillis;
    if (!probe()) return 0;
  }

  if ((long)(now - _next_poll) < 0) return 0;
  _next_poll = now + kPollMillis;

  // One byte per read. An idle keyboard answers 0; a keyboard that has gone away
  // answers nothing, and is probed for again rather than read every 20 ms.
  if (_wire->requestFrom(cardkb::kAddress, (uint8_t)1) != 1) {
    _present = false;
    _next_probe = now + kReprobeMillis;
    return 0;
  }
  const int b = _wire->read();
  return (b < 0) ? 0 : (uint8_t)b;
}
