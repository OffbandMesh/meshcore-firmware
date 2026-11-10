#include "TouchFrame.h"

namespace offband {
namespace gui {

Gt911Frame parseGt911(uint8_t status, const uint8_t* point, size_t len, TouchPoint* out) {
  if (!(status & 0x80)) return Gt911Frame::NotReady;
  const uint8_t count = status & 0x0F;
  if (count > 5 || !out) return Gt911Frame::Invalid;
  if (count == 0) {
    out->pressed = false;
    return Gt911Frame::Released;
  }
  if (!point || len < kGt911PointLen) return Gt911Frame::Invalid;
  const uint16_t x = uint16_t(point[1] | (point[2] << 8));
  const uint16_t y = uint16_t(point[3] | (point[4] << 8));
  if (x > 4095 || y > 4095) return Gt911Frame::Invalid;   // a glitch, not a coordinate
  out->pressed = true;
  out->x = x;
  out->y = y;
  return Gt911Frame::Pressed;
}

bool isGt911ProductId(const uint8_t* id, size_t len) {
  return id && len >= 3 && id[0] == '9' && id[1] == '1' && id[2] == '1';
}

bool Gt911Tracker::update(uint8_t status, const uint8_t* point, size_t len, TouchPoint* out) {
  if (!out) return false;
  TouchPoint next = _last;
  const Gt911Frame f = parseGt911(status, point, len, &next);
  if (f == Gt911Frame::Invalid) return false;
  if (f == Gt911Frame::NotReady) {
    if (_last.pressed && _stale < kGt911StaleReads && ++_stale == kGt911StaleReads) _last.pressed = false;
  } else {
    _last = next;
    _stale = 0;
  }
  *out = _last;
  return true;
}

bool parseChsc6xFrame(const uint8_t* buf, size_t len, TouchPoint* out) {
  if (!buf || !out || len < kChsc6xFrameLen) return false;

  const bool all_zero = buf[2] == 0x00 && buf[3] == 0x00 && buf[4] == 0x00 && buf[6] == 0x00;
  const bool all_ones = buf[2] == 0xFF && buf[3] == 0xFF && buf[4] == 0xFF && buf[6] == 0xFF;
  if (all_zero || all_ones) return false;

  const uint8_t points = buf[2] & 0x07;
  out->pressed = points > 0;
  out->x = uint16_t((buf[3] & 0x0F) << 8) | buf[4];
  out->y = uint16_t((buf[5] & 0x0F) << 8) | buf[6];
  return true;
}

size_t cst66xxReportLen(uint8_t counts) {
  const unsigned entries = unsigned(counts & 0x0F) + unsigned(counts >> 4);
  if (entries > kCst66xxMaxEntries) return 0;
  return kCst66xxHeadLen + kCst66xxEntryLen * entries;
}

Cst66xxFrame parseCst66xx(const uint8_t* buf, size_t len, TouchPoint* out) {
  if (!buf || !out || len < kCst66xxHeadLen) return Cst66xxFrame::Invalid;
  const size_t need = cst66xxReportLen(buf[3]);
  if (need == 0 || len < need) return Cst66xxFrame::Invalid;

  uint16_t sum = 0x55;
  for (size_t i = kCst66xxHeadLen; i < need; i++) sum = uint16_t(sum + buf[i]);
  if (sum != uint16_t(buf[0] | (buf[1] << 8))) return Cst66xxFrame::Invalid;

  if (buf[2] != 0xFF) return Cst66xxFrame::Other;

  const uint8_t keys = buf[3] >> 4, fingers = buf[3] & 0x0F;
  for (uint8_t i = 0; i < fingers; i++) {
    const uint8_t* e = buf + kCst66xxHeadLen + kCst66xxEntryLen * (keys + i);
    if ((e[4] >> 4) == 0) continue;   // this finger is up
    out->pressed = true;
    out->x = uint16_t(e[0] | ((e[3] & 0x0F) << 8));
    out->y = uint16_t(e[1] | ((e[3] & 0xF0) << 4));
    return Cst66xxFrame::Pressed;
  }
  out->pressed = false;
  return Cst66xxFrame::Released;
}

bool Cst66xxTracker::report(const uint8_t* buf, size_t len, TouchPoint* out) {
  if (!out) return false;
  TouchPoint next = _last;
  const Cst66xxFrame f = parseCst66xx(buf, len, &next);
  if (f == Cst66xxFrame::Invalid) return false;
  // A gesture or proximity report still shows the chip is reporting.
  if (f != Cst66xxFrame::Other) _last = next;
  _stale = 0;
  *out = _last;
  return true;
}

void Cst66xxTracker::idle(TouchPoint* out) {
  if (_last.pressed && _stale < kCst66xxStaleReads && ++_stale == kCst66xxStaleReads) _last.pressed = false;
  if (out) *out = _last;
}

void Cst66xxTracker::reset() {
  _last.pressed = false;
  _stale = 0;
}

bool FailedReadGuard::update(bool ok, bool pressed) {
  if (ok) {
    _failed = 0;
    _pressed = pressed;
  } else if (_failed < kReleaseAfterFailedReads && ++_failed == kReleaseAfterFailedReads) {
    _pressed = false;
  }
  return _pressed;
}

void mapTouch(const TouchMapping& m, uint16_t* x, uint16_t* y) {
  uint16_t tx = *x, ty = *y;
  if (m.swap_xy) {
    uint16_t t = tx;
    tx = ty;
    ty = t;
  }
  if (tx >= m.w) tx = m.w - 1;
  if (ty >= m.h) ty = m.h - 1;
  if (m.mirror_x) tx = uint16_t(m.w - 1 - tx);
  if (m.mirror_y) ty = uint16_t(m.h - 1 - ty);
  *x = tx;
  *y = ty;
}

}  // namespace gui
}  // namespace offband
