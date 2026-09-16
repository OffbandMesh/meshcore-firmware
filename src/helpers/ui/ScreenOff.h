#pragma once

#include <stdint.h>

// Offband (#1245): how long a display waits before it blanks. Every ui-new board has an
// auto-off, so this is not badge UI and does not live in BadgeLayout.h -- the badge's
// Screen off row, its step table and its names do.
//
// The preference is seconds, and 0 means "whatever this board was compiled with", which
// is what a board with no way to set it keeps. Free of Arduino, so the arithmetic every
// auto-off deadline is built from can be unit-tested.
namespace offband {

// The wait, in milliseconds. The compiled value is carried in milliseconds the whole way:
// dividing it into seconds first would truncate a board compiled with less than a second
// to no wait at all, and would quietly shorten one compiled with, say, 15500.
inline uint32_t screenOffMillis(uint16_t pref_secs, uint32_t compiled_ms) {
  return pref_secs != 0 ? (uint32_t)pref_secs * 1000UL : compiled_ms;
}

// What a Settings row shows for the same pair. A compiled value that is not a whole
// number of seconds rounds up, so the row never claims a shorter wait than the display
// takes.
inline uint16_t screenOffSecsShown(uint16_t pref_secs, uint32_t compiled_ms) {
  if (pref_secs != 0) return pref_secs;
  // Saturate before rounding up, not after: near the top of a uint32 the `+ 999` wraps
  // and the answer comes back as no wait at all.
  if (compiled_ms > 65535UL * 1000UL) return 65535;
  return (uint16_t)((compiled_ms + 999UL) / 1000UL);
}

}  // namespace offband
