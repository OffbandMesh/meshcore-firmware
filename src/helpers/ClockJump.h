#pragma once

// #1233: notices the clock being set. The phone, the GPS or the CLI can set the RTC at
// any time, and times stamped before that are then off by the change. Each check
// compares the clock's advance with the millisecond clock's since the last one; a
// difference beyond kToleranceSecs is a set, returned as the jump. Checks can come
// every few milliseconds or seconds apart: a clock that keeps pace with millis never
// reads as a jump, and millis wrapping doesn't either.
//
// A jump also says which stamps came from the old clock: those from this run, taken
// between the first reading (moved along by any earlier jumps) and the moment before
// the set. Stamps from before this run lie below that, because at boot MeshCore starts
// the clock just past the newest one saved (bootstrapRTCfromContacts).
//
// Pure: no Arduino and no mesh.

#include <stdint.h>

namespace offband {

class ClockJump {
public:
  static constexpr int64_t kToleranceSecs = 5;

  struct Jump {
    int64_t by;      // seconds; 0 for none
    uint32_t from;   // stamps from the old clock this run lie in [from, to]
    uint32_t to;
    bool covers(uint32_t t) const { return by != 0 && t >= from && t <= to; }
  };

  static uint32_t moved(uint32_t t, int64_t by) {
    const int64_t v = (int64_t)t + by;
    return v < 0 ? 0u : (v > (int64_t)0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)v);
  }

  // The jump since the last check, if any. The first check only takes a reading.
  Jump check(uint32_t wall, uint32_t ms) {
    Jump j = {0, 0, 0};
    if (!_seen) {
      _seen = true;
      _start = wall;
    } else {
      const int64_t expected = (int64_t)_wall + (int64_t)((uint32_t)(ms - _ms) / 1000);
      const int64_t d = (int64_t)wall - expected;
      if (d > kToleranceSecs || d < -kToleranceSecs) {
        j.by = d;
        j.from = _start;
        j.to = moved(0, expected);
        _start = moved(_start, d);
      }
    }
    _wall = wall;
    _ms = ms;
    return j;
  }

private:
  bool _seen = false;
  uint32_t _start = 0;   // the first reading this run, moved along by each jump since
  uint32_t _wall = 0;
  uint32_t _ms = 0;
};

}  // namespace offband
