#pragma once

// #1233: notices the clock being set. The phone, the GPS or the CLI can set the RTC at
// any time, and times stamped before that are then off by the change. Each check
// compares the clock's advance with the millisecond clock's since the last one; a
// difference beyond kToleranceSecs is a set, returned as the jump. Checks can come
// every few milliseconds or seconds apart: a clock that keeps pace with millis never
// reads as a jump, and millis wrapping doesn't either.
//
// Pure: no Arduino and no mesh.

#include <stdint.h>

namespace offband {

class ClockJump {
public:
  static constexpr int64_t kToleranceSecs = 5;

  // The jump since the last check, in seconds, or 0. The first check only takes a
  // reading.
  int64_t check(uint32_t wall, uint32_t ms) {
    int64_t jump = 0;
    if (_seen) {
      const int64_t expected = (int64_t)_wall + (int64_t)((uint32_t)(ms - _ms) / 1000);
      const int64_t d = (int64_t)wall - expected;
      if (d > kToleranceSecs || d < -kToleranceSecs) jump = d;
    }
    _seen = true;
    _wall = wall;
    _ms = ms;
    return jump;
  }

private:
  bool _seen = false;
  uint32_t _wall = 0;
  uint32_t _ms = 0;
};

}  // namespace offband
