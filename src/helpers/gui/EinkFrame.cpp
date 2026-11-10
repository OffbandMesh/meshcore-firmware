#include "EinkFrame.h"

namespace offband {
namespace gui {

void EinkArea::add(int16_t ax1, int16_t ay1, int16_t ax2, int16_t ay2) {
  if (ax2 < ax1 || ay2 < ay1) return;
  if (empty()) {
    x1 = ax1;
    y1 = ay1;
    x2 = ax2;
    y2 = ay2;
    return;
  }
  if (ax1 < x1) x1 = ax1;
  if (ay1 < y1) y1 = ay1;
  if (ax2 > x2) x2 = ax2;
  if (ay2 > y2) y2 = ay2;
}

EinkPush EinkPacer::due(uint32_t now_ms) const {
  if (_dirty.empty()) return EinkPush::None;
  if (!_pushed_once) return EinkPush::Full;
  // Unsigned subtraction, so millis() wrapping doesn't stall the panel.
  if (now_ms - _last_push_ms < _min_gap_ms) return EinkPush::None;
  if (_force_full || _partials >= _full_every - 1) return EinkPush::Full;
  return EinkPush::Partial;
}

void EinkPacer::pushed(EinkPush kind, uint32_t now_ms) {
  if (kind == EinkPush::None) return;
  _dirty = EinkArea();
  _last_push_ms = now_ms;
  _pushed_once = true;
  if (kind == EinkPush::Full) {
    _partials = 0;
    _force_full = false;
  } else {
    _partials++;
  }
}

}  // namespace gui
}  // namespace offband
