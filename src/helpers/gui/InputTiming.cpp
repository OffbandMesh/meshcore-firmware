#include "InputTiming.h"

namespace offband {
namespace gui {

bool ButtonTracker::update(bool down, uint32_t now_ms, PressKind* kind) {
  if (down != _raw) {
    _raw = down;
    _raw_since = now_ms;
  }
  if (_raw == _stable || now_ms - _raw_since < kDebounceMs) {
    if (_stable && !_long_sent && now_ms - _down_at >= kLongHoldMs) {
      _long_sent = true;
      if (kind) *kind = PressKind::LongHold;
      return true;
    }
    return false;
  }

  _stable = _raw;
  if (_stable) {
    // Timed from when the level settled. Contact bounce moves both ends by a
    // few ms, which doesn't matter against 1.2 s and 3 s thresholds.
    _down_at = _raw_since;
    _long_sent = false;
    return false;
  }
  if (_long_sent) return false;
  if (kind) *kind = classifyPress(_raw_since - _down_at);
  return true;
}

void ClickCounter::click(uint32_t now_ms) {
  if (_count < 3) _count++;
  _last_ms = now_ms;
}

uint8_t ClickCounter::poll(uint32_t now_ms, bool button_down) {
  if (!_count || button_down || now_ms - _last_ms < kMultiClickMs) return 0;
  return take();
}

uint8_t ClickCounter::take() {
  uint8_t n = _count;
  _count = 0;
  return n;
}

ScreenState screenStateFor(uint32_t idle_ms, uint32_t dim_after_ms, uint32_t off_after_ms,
                           bool always_on) {
  if (always_on) return ScreenState::On;
  if (off_after_ms && idle_ms >= off_after_ms) return ScreenState::Off;
  if (dim_after_ms && idle_ms >= dim_after_ms) return ScreenState::Dim;
  return ScreenState::On;
}

}  // namespace gui
}  // namespace offband
