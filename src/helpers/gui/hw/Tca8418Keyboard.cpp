#include "Tca8418Keyboard.h"

#include <Arduino.h>
#include <MeshLog.h>

using namespace offband::gui;

bool Tca8418Keyboard::readReg(uint8_t reg, uint8_t* v) {
  _wire.beginTransmission(kTca8418Addr);
  _wire.write(reg);
  if (_wire.endTransmission(false) != 0) return false;
  if (_wire.requestFrom(kTca8418Addr, uint8_t(1)) != 1) return false;
  const int b = _wire.read();
  if (b < 0) return false;
  *v = uint8_t(b);
  return true;
}

bool Tca8418Keyboard::writeReg(uint8_t reg, uint8_t v) {
  _wire.beginTransmission(kTca8418Addr);
  _wire.write(reg);
  _wire.write(v);
  return _wire.endTransmission() == 0;
}

bool Tca8418Keyboard::begin() {
  _present = false;
  _wire.beginTransmission(kTca8418Addr);
  if (_wire.endTransmission() != 0) return false;

  const MatrixSelect m = tca8418MatrixSelect(kProKeyRows, kProKeyCols);
  if (!writeReg(tca8418::kRegKpGpio1, m.rows) || !writeReg(tca8418::kRegKpGpio2, m.cols_lo) ||
      !writeReg(tca8418::kRegKpGpio3, m.cols_hi)) {
    return false;
  }
  // Read one back: something else answering at 0x34 is not taken for the
  // keyboard.
  uint8_t rows = 0;
  if (!readReg(tca8418::kRegKpGpio1, &rows) || rows != m.rows) return false;
  if (!writeReg(tca8418::kRegCfg, tca8418::kCfgValue)) return false;

  // Whatever the chip collected before we were listening is dropped.
  for (uint8_t i = 0; i < 2 * kTca8418FifoDepth; i++) {
    uint8_t e = 0;
    if (!readReg(tca8418::kRegKeyEventA, &e) || e == 0) break;
  }
  writeReg(tca8418::kRegIntStat, tca8418::kIntAll);

  if (_int_pin >= 0) pinMode(_int_pin, INPUT_PULLUP);   // open drain, active low
  if (_led_pin >= 0) pinMode(_led_pin, OUTPUT);
  _state.reset();
  _overflow_pending = false;
  _bus_fail_run = 0;
  _present = true;
  return true;
}

void Tca8418Keyboard::busError() {
  _stats.bus_errors++;
  // A bus that stays down may have eaten a release: let go of anything held,
  // so Backspace can't repeat on forever.
  if (++_bus_fail_run >= 5) {
    _bus_fail_run = 0;
    _state.reset();
  }
}

void Tca8418Keyboard::service() {
  if (!_present) return;
  const uint32_t now = millis();
  const bool int_low = _int_pin >= 0 && digitalRead(_int_pin) == LOW;
  if (!int_low && now - _last_poll < kSafetyPollMs) return;
  _last_poll = now;

  uint8_t stat = 0, count = 0;
  if (!readReg(tca8418::kRegIntStat, &stat) || !readReg(tca8418::kRegKeyLckEc, &count)) {
    busError();
    return;
  }
  count &= tca8418::kEventCountMask;
  if (count > kTca8418FifoDepth) count = kTca8418FifoDepth;
  uint8_t taken = 0;
  for (uint8_t i = 0; i < count; i++) {
    uint8_t e = 0;
    if (!readReg(tca8418::kRegKeyEventA, &e)) {
      busError();
      return;
    }
    if (e == 0) break;
    // A full queue lost this byte, perhaps a release: start clean, as for
    // the chip's own overflow.
    if (!_queue.push(e)) _overflow_pending = true;
    taken++;
  }
  _bus_fail_run = 0;
  _stats.events += taken;
  if (taken && !int_low && _int_pin >= 0) _stats.int_missed++;
  if (stat & tca8418::kIntOvrFlow) {
    _stats.overflows++;
    _overflow_pending = true;
  }
  if (stat & tca8418::kIntAll) writeReg(tca8418::kRegIntStat, stat & tca8418::kIntAll);
}

uint8_t Tca8418Keyboard::read(bool* repeat) {
  if (repeat) *repeat = false;
  if (!_present) return 0;
  if (!_queue.size()) service();
  if (_overflow_pending) {
    // The chip lost events, perhaps a release: start clean.
    _overflow_pending = false;
    _state.reset();
  }
  const uint32_t now = millis();
  uint8_t raw;
  while (_queue.pop(&raw)) {
    uint8_t row, col;
    bool pressed;
    if (!tca8418Decode(raw, &row, &col, &pressed)) continue;
#ifdef OFFBAND_GUI_PERF
    // Diag builds: which matrix position each press came from, for the
    // bench press of every key (C2 #2150). Positions only, never what was
    // typed, and only the first 100 presses after boot, so a password typed
    // later doesn't reach the log.
    static uint8_t s_logged = 0;
    if (pressed && s_logged < 100) {
      s_logged++;
      const ProKeyCap cap = proKeyAt(row, col);
      mesh_log_line(MLOG_BOOT, "[gui] key %u: row=%u col=%u (from left %u) kind=%u%s\n", unsigned(s_logged),
                    unsigned(row), unsigned(col), unsigned(proKeyPhysicalCol(col)), unsigned(cap.kind),
                    s_logged == 100 ? " (key log ends)" : "");
    }
#endif
    ProKeyOut out;
    if (_state.event(row, col, pressed, now, &out)) return out.byte;
  }
  ProKeyOut out;
  if (_state.repeat(now, &out)) {
    if (repeat) *repeat = out.repeat;
    return out.byte;
  }
  return 0;
}

bool Tca8418Keyboard::setBacklight(uint8_t duty) {
  if (_led_pin < 0) return false;
  digitalWrite(_led_pin, duty ? HIGH : LOW);
  return true;
}

Tca8418Keyboard::Stats Tca8418Keyboard::takeStats() {
  _stats.dropped = _queue.dropped() - _dropped_seen;
  _dropped_seen = _queue.dropped();
  const Stats s = _stats;
  _stats = Stats{};
  return s;
}
