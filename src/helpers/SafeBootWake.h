#pragma once

// #1687: how SafeBoot leaves a low-battery sleep, as rules with no Arduino in them.
//
// SafeBoot used to arm its nRF52 button wake on `PIN_USER_BTN` as if it were a chip pin,
// with a pull-up and SENSE_LOW, on every board. Both assumptions were wrong somewhere:
//
//   - On the QCC badge `PIN_USER_BTN` is an ARDUINO pin (6 -> P1.00); the HAL took it as
//     raw P0.06, the buzzer MOSFET gate, which is held low. Measured on QCC2 2026-10-27
//     (#1689): P0.06 reads 0 even under the pull-up, P1.00 reads 1. SENSE_LOW was true the
//     instant SYSTEM_OFF was entered: an immediate wake, a reset, a loop every 128 ms.
//   - On the T1000-E the button is ACTIVE-HIGH, so pull-up + SENSE_LOW is satisfied at
//     idle and the same loop follows.
//
// The rules below are the fix's decisions, kept pure so the host tests can walk every
// case: which pull and which sense level follow from the button's polarity, whether the
// button can be armed at all, and what the device will actually do -- which is what the
// log line has to say (#1690).

#include <stdint.h>

namespace offband {

// What ends a SafeBoot sleep. Each is a real behavior of the platform, not a wish.
enum class SafeBootWake : uint8_t {
  TimedDeepSleep,  // ESP32: deep sleep on a timer, then a reboot and a recheck
  BatteryRise,     // nRF52 SYSTEM_OFF; LPCOMP wakes it when the cell voltage rises
  ButtonOnly,      // nRF52 SYSTEM_OFF; only a button press wakes it
  TimedRestart,    // nRF52 stays on, waits, then restarts to recheck
};

// How to arm a button as a SYSTEM_OFF wake source.
struct ButtonSense {
  bool usable;      // false: the button already reads "pressed", so SENSE would fire at once
  bool sense_high;  // wake on HIGH (an active-high button) or on LOW
  bool pull_down;   // the idle pull: down for an active-high button, up for active-low
};

// `pressed_high` is the button's pressed level (USER_BTN_PRESSED == HIGH).
// `idle_level` is the pin as read AFTER the idle pull has been applied.
//
// Arming a SENSE whose condition is already true wakes the chip the moment it sleeps, so
// a button that reads pressed -- held down, a stuck switch, or the wrong pin -- is not
// usable, and the caller must fall back rather than sleep into a loop.
inline ButtonSense chooseButtonSense(bool pressed_high, int idle_level) {
  ButtonSense b;
  b.sense_high = pressed_high;
  b.pull_down = pressed_high;
  const int pressed = pressed_high ? 1 : 0;
  b.usable = (idle_level == 0 || idle_level == 1) && idle_level != pressed;
  return b;
}

// How long SafeBoot waits for a held button to be let go before calling it stuck. The press
// that wakes a ButtonOnly sleep is still down when the restarted firmware reaches this check,
// a few milliseconds in -- a person's tap lasts 100-300 ms and a deliberate press a second or
// more. Read once, that press looked stuck and disarmed the next sleep, so only the first
// press after a reset ever worked (QCC1/QCC2, 2026-10-29). Five seconds covers any human
// press; a line still pressed after that is a real fault.
constexpr uint32_t kButtonReleaseWaitMs = 5000;
constexpr uint32_t kButtonReleasePollMs = 10;

// Poll the button until it reads idle or `max_wait_ms` runs out, and return the last level
// read. `read()` returns the pin level; `wait(ms)` sleeps and keeps any watchdog fed.
template <typename ReadFn, typename WaitFn>
inline int waitForButtonIdle(bool pressed_high, uint32_t max_wait_ms, ReadFn read, WaitFn wait) {
  const int idle = pressed_high ? 0 : 1;
  int level = read();
  for (uint32_t waited = 0; level != idle && waited < max_wait_ms;) {
    const uint32_t left = max_wait_ms - waited;
    const uint32_t step = left < kButtonReleasePollMs ? left : kButtonReleasePollMs;
    wait(step);
    waited += step;
    level = read();
  }
  return level;
}

// Which nRF52 sleep to take. LPCOMP first: it is the one that recovers on its own when a
// charger or the sun brings the cell back. A usable button next: a true SYSTEM_OFF at a
// few microamps, woken by a press. Otherwise the board stays on and restarts on a timer --
// costing milliamps, but never a loop.
inline SafeBootWake chooseNrf52Wake(bool has_battery_comparator, bool button_usable) {
  if (has_battery_comparator) return SafeBootWake::BatteryRise;
  if (button_usable) return SafeBootWake::ButtonOnly;
  return SafeBootWake::TimedRestart;
}

// The words the refusal line uses for what happens next (#1690). The old line said
// "Sleep 600s" on every platform -- an ESP32 concept printed on hardware that cannot time
// a sleep. `secs` is only meaningful for the two timed kinds. `button_armed` says whether a
// press will also wake it, so a battery-rise sleep never promises a button that is not set.
inline const char* safeBootWakeText(SafeBootWake w, bool button_armed) {
  switch (w) {
    case SafeBootWake::TimedDeepSleep:
      return "deep sleep, then restart and recheck after";
    case SafeBootWake::BatteryRise:
      return button_armed ? "powering off; wakes when the battery recovers or the button is pressed"
                          : "powering off; wakes when the battery recovers";
    case SafeBootWake::ButtonOnly:
      return "powering off; press the button to retry";
    case SafeBootWake::TimedRestart:
      return "staying on, then restart and recheck after";
  }
  return "?";
}

inline bool safeBootWakeIsTimed(SafeBootWake w) {
  return w == SafeBootWake::TimedDeepSleep || w == SafeBootWake::TimedRestart;
}

}  // namespace offband
