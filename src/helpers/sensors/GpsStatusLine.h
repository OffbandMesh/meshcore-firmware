#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Offband (#149, #1247): the one ASCII rendering of the live GPS state. Two callers want
// different amounts of it, so it is a pure function over a snapshot and free of Arduino,
// which also lets the rendered text itself be unit-tested.
//
// lat, lon and alt are integers in the SELF_INFO wire units (1e-6 deg, cm). newlib-nano
// on nRF52 omits %f from printf, so a float here would render garbage.
namespace offband {

struct GpsSnapshot {
  bool detected = false;
  bool active = false;
  bool fix = false;
  uint32_t baud = 0;
  long lat_ud = 0;      // 1e-6 degrees; 0,0 when there is nothing believable to carry
  long lon_ud = 0;
  long alt_cm = 0;
  long sats = 0;
  long epoch = 0;       // 0 until the GPS has given a date
};

// True for NaN and +/-inf. Decided on the bits, like qcc::isNonFinite: device builds use
// -Ofast, whose finite-math assumption lets the compiler drop a floating-point NaN test.
inline bool isNonFiniteDouble(double d) {
  uint64_t u;
  memcpy(&u, &d, sizeof u);
  return (u & 0x7FF0000000000000ULL) == 0x7FF0000000000000ULL;
}

// #1247, the owner: whether the stored coordinates are ones worth believing. This is
// deliberately not the same question as a fix -- a badge that has lost its fix still
// carries the last real position, and `fix=0 pos=valid` is the combination worth
// reading. 0,0 is his example of a position that is not sane: it is where an unset
// pair sits, off the coast of Africa, and no badge is ever there.
inline bool positionIsSane(long lat_ud, long lon_ud) {
  if (lat_ud == 0 && lon_ud == 0) return false;
  if (lat_ud < -90000000L || lat_ud > 90000000L) return false;
  if (lon_ud < -180000000L || lon_ud > 180000000L) return false;
  return true;
}

// Degrees to the wire's microdegrees, for a value that may be anything at all. Casting a
// NaN, an infinity or a value too large for a long is undefined behavior and on ARM comes
// back as a plausible-looking number, so the range is checked in double space first --
// before the cast, not after it. Either both coordinates survive or neither does: a real
// longitude beside a rejected latitude would read as a position on the prime meridian.
inline bool toSanePosition(double lat_deg, double lon_deg, long& lat_ud, long& lon_ud) {
  lat_ud = 0;
  lon_ud = 0;
  if (isNonFiniteDouble(lat_deg) || isNonFiniteDouble(lon_deg)) return false;
  if (lat_deg < -90.0 || lat_deg > 90.0 || lon_deg < -180.0 || lon_deg > 180.0) return false;
  const long lat = (long)(lat_deg * 1000000.0);
  const long lon = (long)(lon_deg * 1000000.0);
  if (!positionIsSane(lat, lon)) return false;
  lat_ud = lat;
  lon_ud = lon;
  return true;
}

// `with_position` carries lat and lon. Without them the line still says whether the
// position is sane, so a reader loses where the device is and nothing else: whether the
// GPS is wired, powered, talking, locked, how many satellites and how high all remain.
//
// `pos=` goes last so the form that keeps the coordinates is the old line with a field
// appended -- a client reading it by position sees what it always saw.
//
// Returns the bytes written, never what would have been written. snprintf's own answer
// overruns `cap` on truncation, and the base SensorManager::getGpsStatusText answers 0
// for "nothing written", so a length that outruns the buffer would be the odd one out.
inline size_t formatGpsStatus(const GpsSnapshot& g, bool with_position, char* out, size_t cap) {
  const char* pos = positionIsSane(g.lat_ud, g.lon_ud) ? "valid" : "invalid";
  int n;
  if (with_position) {
    n = snprintf(out, cap,
      "detected=%d active=%d fix=%d baud=%lu lat=%ld lon=%ld alt_cm=%ld sats=%ld time=%ld pos=%s",
      g.detected ? 1 : 0, g.active ? 1 : 0, g.fix ? 1 : 0,
      (unsigned long)g.baud, g.lat_ud, g.lon_ud, g.alt_cm, g.sats, g.epoch, pos);
  } else {
    n = snprintf(out, cap,
      "detected=%d active=%d fix=%d baud=%lu alt_cm=%ld sats=%ld time=%ld pos=%s",
      g.detected ? 1 : 0, g.active ? 1 : 0, g.fix ? 1 : 0,
      (unsigned long)g.baud, g.alt_cm, g.sats, g.epoch, pos);
  }
  if (n < 0 || cap == 0) return 0;
  return ((size_t)n < cap) ? (size_t)n : cap - 1;
}

}  // namespace offband
