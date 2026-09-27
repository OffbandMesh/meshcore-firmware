#pragma once
#include <stdint.h>

// #1072: settle-time debounce for a USB "link up" reading, with flap
// accounting, used by ArduinoSerialInterface's [usb] lifecycle lines.
//
// A state is reported only after it has held for kSettleMs: the ESP32
// USB-Serial-JTAG SOF state confirms a disconnect after ~5 ms without SOF, so
// a noisy bus or a host dipping into suspend would otherwise report an
// up/down pair each time, and it is forced true at power-on, which would
// report a phantom "up" on a board with no host.
//
// The settle time must not hide what it smooths over, so raw changes are
// counted:
//   - BLIP_UP / BLIP_DOWN: the link flipped and came back to the reported
//     state within the window (e.g. a fast unplug/replug).
//   - UNSTABLE: kFlapReport changes without settling, at most once per
//     kUnstableRepeatMs, so a link that never settles is still reported.
// The count keeps accumulating across UNSTABLE reports until the next settled
// report, which therefore covers the whole episode.
//
// Pure and Arduino-free: unit-tested natively in test/test_usb_link.
struct UsbLinkTracker {
  enum Event : uint8_t { NONE, UP, DOWN, BLIP_UP, BLIP_DOWN, UNSTABLE };

  static constexpr uint32_t kSettleMs = 1000;
  static constexpr uint16_t kFlapReport = 20;
  static constexpr uint32_t kUnstableRepeatMs = 10000;

  int8_t   reported = -1;      // last reported state: -1 none yet, 0 down, 1 up
  int8_t   candidate = -1;     // raw reading waiting out the settle time
  uint32_t since = 0;          // when the candidate last changed
  uint16_t flaps = 0;          // raw changes since the last settled report
  bool     unstable_sent = false;
  uint32_t unstable_ms = 0;    // when the last UNSTABLE was reported

  // Feed one raw reading taken at `now_ms`. Returns what to report; for any
  // event other than NONE, *flaps_out receives the count to print with it.
  Event update(bool up, uint32_t now_ms, uint16_t* flaps_out) {
    const int8_t now = up ? 1 : 0;
    if (now != candidate) {
      candidate = now;
      since = now_ms;
      if (reported < 0) return NONE;   // power-on settling is not a flap
      if (flaps < UINT16_MAX) flaps++;
      if (flaps >= kFlapReport &&
          (!unstable_sent || now_ms - unstable_ms >= kUnstableRepeatMs)) {
        unstable_sent = true;
        unstable_ms = now_ms;
        *flaps_out = flaps;
        return UNSTABLE;
      }
      return NONE;
    }
    if (now_ms - since < kSettleMs) return NONE;
    if (now == reported && flaps == 0) return NONE;   // settled, nothing new
    const bool changed = (now != reported);
    reported = now;
    *flaps_out = flaps;
    flaps = 0;
    if (changed) return now ? UP : DOWN;
    return now ? BLIP_UP : BLIP_DOWN;
  }
};
