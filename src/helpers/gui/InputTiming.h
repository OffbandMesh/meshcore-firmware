#pragma once

// Button debouncing and the screen's on/dim/off timing for the GUI (#1443).
// Arduino-free: callers pass the raw button level and millis().

#include <stdint.h>

#include "InputMap.h"

namespace offband {
namespace gui {

constexpr uint32_t kDebounceMs = 20;

// One physical button. A press is reported once, on release, with its
// length classified, so holding for a long hold never fires a hold first.
class ButtonTracker {
public:
  // `down` is the debounced-to-be level (true = pressed). Returns true when
  // a press has just completed, with its kind in *kind.
  bool update(bool down, uint32_t now_ms, PressKind* kind);

private:
  bool _stable = false;      // debounced level
  bool _raw = false;         // last raw level seen
  uint32_t _raw_since = 0;   // when the raw level last changed
  uint32_t _down_at = 0;     // when the debounced press began
};

enum class ScreenState : uint8_t { On, Dim, Off };

// The screen's state `idle_ms` after the last input. `dim_after_ms` or
// `off_after_ms` of 0 disables that step; `always_on` keeps it fully on.
ScreenState screenStateFor(uint32_t idle_ms, uint32_t dim_after_ms, uint32_t off_after_ms,
                           bool always_on);

}  // namespace gui
}  // namespace offband
