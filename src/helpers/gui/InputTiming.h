#pragma once

// Button debouncing and the screen's on/dim/off timing for the GUI (#1443).
// Arduino-free: callers pass the raw button level and millis().

#include <stdint.h>

#include "InputMap.h"

namespace offband {
namespace gui {

constexpr uint32_t kDebounceMs = 20;

// One physical button. A press or hold is reported once, on release, with
// its length classified, so a long hold never fires a hold first. A long
// hold is reported the moment it reaches kLongHoldMs, while still held, and
// its release reports nothing.
class ButtonTracker {
public:
  // `down` is the debounced-to-be level (true = pressed). Returns true when
  // a press has just been classified, with its kind in *kind.
  bool update(bool down, uint32_t now_ms, PressKind* kind);
  bool isDown() const { return _stable; }

private:
  bool _stable = false;      // debounced level
  bool _raw = false;         // last raw level seen
  bool _long_sent = false;   // this press already reported its long hold
  uint32_t _raw_since = 0;   // when the raw level last changed
  uint32_t _down_at = 0;     // when the debounced press began
};

// Counts the clicks of one button: each click (a completed short press) is
// added, and once kMultiClickMs passes after a release without the button
// going down again, the total is reported, capped at 3. The window runs from
// release to the next press, so how long a click is held doesn't shorten it.
class ClickCounter {
public:
  void click(uint32_t now_ms);
  // The click count (1-3) once the window has closed, else 0. Reports once.
  // `button_down` holds the window open while the next click is in progress.
  uint8_t poll(uint32_t now_ms, bool button_down);
  // The pending click count, now, without waiting for the window; used when
  // a hold ends the sequence. Clears it.
  uint8_t take();

private:
  uint8_t _count = 0;
  uint32_t _last_ms = 0;
};

enum class ScreenState : uint8_t { On, Dim, Off };

// The screen's state `idle_ms` after the last input. `dim_after_ms` or
// `off_after_ms` of 0 disables that step; `always_on` keeps it fully on.
ScreenState screenStateFor(uint32_t idle_ms, uint32_t dim_after_ms, uint32_t off_after_ms,
                           bool always_on);

}  // namespace gui
}  // namespace offband
