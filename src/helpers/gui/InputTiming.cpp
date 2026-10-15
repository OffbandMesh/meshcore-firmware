#include "InputTiming.h"

namespace offband {
namespace gui {

bool ButtonTracker::update(bool down, uint32_t now_ms, PressKind* kind) {
  if (down != _raw) {
    _raw = down;
    _raw_since = now_ms;
  }
  if (_raw == _stable || now_ms - _raw_since < kDebounceMs) return false;

  _stable = _raw;
  if (_stable) {
    // Timed from when the level settled. Contact bounce moves both ends by a
    // few ms, which doesn't matter against 600 ms and 3 s thresholds.
    _down_at = _raw_since;
    return false;
  }
  if (kind) *kind = classifyPress(_raw_since - _down_at);
  return true;
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
