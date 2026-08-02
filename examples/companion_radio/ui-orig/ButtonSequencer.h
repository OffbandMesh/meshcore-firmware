#pragma once
#include <stdint.h>
#include <stddef.h>

// #527: the press-counting state machine, separated from the hardware.
//
// WHY THIS EXISTS AS ITS OWN THING
//
// The old Button did edge detection by polling: update() read the pin, and a press
// only counted if the level was observed STABLE across samples spanning >50 ms. When
// UITask::loop() ran late -- and on a board also servicing radio, BLE, GPS and sensors
// it does -- a press could begin and end between two reads and be discarded entirely.
// No counter increment, no event, no log. A dropped click was indistinguishable from
// no click, which is exactly how a triple-press silently resolved as a single.
//
// This class never touches a pin. It consumes (timestamp, level) EDGES that were
// captured when they happened, and decides what they mean whenever it is asked. A loop
// that stalls 800 ms and then feeds it four edges stamped 120 ms apart still resolves a
// QUADRUPLE, because it reasons about when things HAPPENED rather than when it looked.
// Lateness costs latency, never correctness.
//
// It is also plain C++ with no Arduino dependency, so the whole thing is unit-testable
// against synthetic edges -- bursts, dropped releases, bounce storms. None of the
// previous implementation ever had a test.

// DEBOUNCE, AND WHY IT IS THIS SMALL
//
// Debounce is genuinely needed. Switch contacts do not close cleanly: they chatter,
// making and breaking several times over the first few milliseconds. Feed that chatter
// in raw and the counter increments on every spurious release -- one press reports as a
// QUADRUPLE. That failure is no better than the one this file exists to fix.
//
// But it only has to outlast the CHATTER, which on a tactile dome is single-digit
// milliseconds. The 50 ms inherited from the original implementation was ~10x that, and
// it was destroying real input: a fast double-tap has each press and each gap well
// under 50 ms, so the first release was thrown away, the second press stopped looking
// like a transition, and the whole gesture collapsed into one click. 10 ms is the
// conventional figure for this kind of switch and leaves room for taps five times
// faster than a human can produce.
//
// The window is also no longer allowed to DELETE anything -- see onEdge(). An edge
// inside the window is deferred and applied when the window closes. That matters for
// more than counting: under the old discard rule, a tap shorter than the window lost
// its release, the machine went on believing the button was held, and fired a LONG
// press three seconds later -- which on the T1000-E is power-off.
#ifndef BUTTON_DEBOUNCE_TIME_MS
#define BUTTON_DEBOUNCE_TIME_MS    10      // outlast contact chatter, nothing more
#endif
#ifndef BUTTON_CLICK_TIMEOUT_MS
#define BUTTON_CLICK_TIMEOUT_MS    500     // max gap between clicks for multi-click
#endif
#ifndef BUTTON_LONG_PRESS_TIME_MS
#define BUTTON_LONG_PRESS_TIME_MS  3000    // held this long = long press
#endif

class ButtonSequencer {
public:
  enum Event : uint8_t {
    EV_NONE = 0,
    EV_PRESS_EDGE,      // a press-down was accepted (wake the display, etc.)
    EV_SHORT,
    EV_DOUBLE,
    EV_TRIPLE,
    EV_QUADRUPLE,
    EV_LONG,
  };

  void reset() {
    _down = false; _click_count = 0; _last_edge_ms = 0;
    _down_at = 0; _up_at = 0; _long_fired = false; _armed = false;
    _pending_valid = false; _pending_level = false;
  }

  // Feed one captured edge. `pressed` is the logical level (true = button down).
  // Returns EV_PRESS_EDGE when a press-down is accepted, otherwise EV_NONE --
  // multi-click results come from tick(), because they are decided by TIME.
  //
  // CALLERS MUST tick(ms) FIRST, draining it, before each onEdge(ms, ...). That is what
  // keeps a deferred edge in front of the edge that follows it; Button::update() does
  // exactly this.
  Event onEdge(uint32_t ms, bool pressed) {
    if (_last_edge_ms != 0 && (uint32_t)(ms - _last_edge_ms) < BUTTON_DEBOUNCE_TIME_MS) {
      // Inside the chatter window: DEFER, never discard. Holding only the most recent
      // level is what makes a bounce burst collapse correctly -- down/up/down/up in
      // 6 ms leaves whatever the contact actually settled on, and that is what gets
      // applied when the window closes.
      _pending_valid = true;
      _pending_level = pressed;
      return EV_NONE;
    }
    _pending_valid = false;      // superseded by a real, in-the-clear edge
    return acceptEdge(ms, pressed);
  }

  // Call with the current time, as often or as rarely as convenient. Returns at most
  // one event per call; call until it returns EV_NONE if you want to drain.
  Event tick(uint32_t now) {
    // A deferred edge comes first: it happened before anything this call could decide.
    if (_pending_valid &&
        (uint32_t)(now - _last_edge_ms) >= BUTTON_DEBOUNCE_TIME_MS) {
      uint32_t at = _last_edge_ms + BUTTON_DEBOUNCE_TIME_MS;
      bool lvl = _pending_level;
      _pending_valid = false;
      Event e = acceptEdge(at, lvl);
      if (e != EV_NONE) return e;
    }
    // Long press while still held. Fires once, and consumes the gesture so the
    // eventual release does not also count as a click.
    if (_down && !_long_fired &&
        (uint32_t)(now - _down_at) >= BUTTON_LONG_PRESS_TIME_MS) {
      _long_fired = true;
      _click_count = 0;
      _armed = false;
      return EV_LONG;
    }
    // Multi-click window closed -> resolve the count.
    if (_armed && !_down && (uint32_t)(now - _up_at) >= BUTTON_CLICK_TIMEOUT_MS) {
      uint8_t n = _click_count;
      _click_count = 0;
      _armed = false;
      switch (n) {
        case 0:  return EV_NONE;
        case 1:  return EV_SHORT;
        case 2:  return EV_DOUBLE;
        case 3:  return EV_TRIPLE;
        default: return EV_QUADRUPLE;   // 4 or more
      }
    }
    return EV_NONE;
  }

  uint8_t pendingClicks() const { return _click_count; }
  bool    isDown()        const { return _down; }

private:
  // Apply an edge that has cleared debounce. The single place _down moves.
  Event acceptEdge(uint32_t ms, bool pressed) {
    if (pressed == _down) return EV_NONE;      // not a transition
    _last_edge_ms = ms;
    _down = pressed;

    if (pressed) {
      _down_at = ms;
      _long_fired = false;
      return EV_PRESS_EDGE;
    }
    // release: count it only if it was a short press. A long press already fired
    // from tick() and consumed the gesture.
    if (!_long_fired && (uint32_t)(ms - _down_at) < BUTTON_LONG_PRESS_TIME_MS) {
      if (_click_count < 250) _click_count++;
      _up_at = ms;
      _armed = true;                            // a multi-click window is now open
    }
    return EV_NONE;
  }

  bool     _down        = false;
  bool     _armed       = false;   // a release happened; a multi-click window is open
  bool     _long_fired  = false;
  bool     _pending_valid = false; // an edge is waiting out the chatter window
  bool     _pending_level = false;
  uint8_t  _click_count = 0;
  uint32_t _last_edge_ms = 0;
  uint32_t _down_at     = 0;
  uint32_t _up_at       = 0;
};
