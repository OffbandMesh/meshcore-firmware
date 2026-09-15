#pragma once

// #1233: time zones for the badge, so it can show the con's local time instead of an
// age. MeshCore keeps UTC only.
//
// A zone is a standard offset and a daylight-saving rule:
// - US: an hour ahead from 02:00 local on the second Sunday of March until 02:00
//   local on the first Sunday of November.
// - EU: an hour ahead from 01:00 UTC on the last Sunday of March until 01:00 UTC on
//   the last Sunday of October.
// The table is append-only: prefs store a zone's index, and 0 means not set.
//
// suggest() picks a zone from a GPS position with coarse boundaries: good enough to
// suggest, and never applied without being confirmed.
//
// Pure: no Arduino and no mesh.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace offband {
namespace tz {

enum class Rule : uint8_t { None, US, EU };

struct Zone {
  const char* name;   // at most 10 characters, for the picker
  int16_t std_min;    // standard offset from UTC, in minutes
  Rule rule;
};

// Append only; never reorder.
constexpr Zone kZones[] = {
  {"not set", 0, Rule::None},
  {"UTC", 0, Rule::None},
  {"Eastern", -300, Rule::US},
  {"Central", -360, Rule::US},
  {"Mountain", -420, Rule::US},
  {"Arizona", -420, Rule::None},
  {"Pacific", -480, Rule::US},
  {"Alaska", -540, Rule::US},
  {"Hawaii", -600, Rule::None},
  {"UK", 0, Rule::EU},              // and Ireland, Portugal
  {"Central EU", 60, Rule::EU},
  {"Eastern EU", 120, Rule::EU},
  {"UTC-12", -720, Rule::None}, {"UTC-11", -660, Rule::None}, {"UTC-10", -600, Rule::None},
  {"UTC-9", -540, Rule::None},  {"UTC-8", -480, Rule::None},  {"UTC-7", -420, Rule::None},
  {"UTC-6", -360, Rule::None},  {"UTC-5", -300, Rule::None},  {"UTC-4", -240, Rule::None},
  {"UTC-3", -180, Rule::None},  {"UTC-2", -120, Rule::None},  {"UTC-1", -60, Rule::None},
  {"UTC+1", 60, Rule::None},    {"UTC+2", 120, Rule::None},   {"UTC+3", 180, Rule::None},
  {"UTC+4", 240, Rule::None},   {"UTC+5", 300, Rule::None},   {"UTC+6", 360, Rule::None},
  {"UTC+7", 420, Rule::None},   {"UTC+8", 480, Rule::None},   {"UTC+9", 540, Rule::None},
  {"UTC+10", 600, Rule::None},  {"UTC+11", 660, Rule::None},  {"UTC+12", 720, Rule::None},
  {"UTC+13", 780, Rule::None},  {"UTC+14", 840, Rule::None},
  {"UTC-3:30", -210, Rule::None}, {"UTC+5:30", 330, Rule::None}, {"UTC+9:30", 570, Rule::None},
};
constexpr int kZoneCount = (int)(sizeof(kZones) / sizeof(kZones[0]));
constexpr int kFirstFixed = 12;   // the plain UTC offsets start here

constexpr int kNotSet = 0, kUtc = 1, kEastern = 2, kCentral = 3, kMountain = 4, kArizona = 5,
              kPacific = 6, kAlaska = 7, kHawaii = 8, kUk = 9, kCentralEu = 10, kEasternEu = 11;

inline bool isSet(int index) { return index > 0 && index < kZoneCount; }
inline const Zone& zone(int index) { return kZones[isSet(index) ? index : 0]; }

// The fixed-offset zone for an offset in minutes, or kNotSet when there's none.
inline int fixedZone(int offset_min) {
  if (offset_min == 0) return kUtc;
  for (int i = kFirstFixed; i < kZoneCount; i++) {
    if (kZones[i].std_min == offset_min) return i;
  }
  return kNotSet;
}

// Days since 1970-01-01 for a civil date (proleptic Gregorian; H. Hinnant's algorithm).
inline int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

inline int yearOfDays(int32_t z) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  return (int)yoe + era * 400 + (m <= 2 ? 1 : 0);
}

inline int weekday(int32_t days) { return (int)(((days % 7) + 11) % 7); }   // 0 is Sunday

// The day of the month of the nth Sunday (1-based) of a month.
inline int nthSunday(int y, unsigned m, int n) {
  const int first = 1 + (7 - weekday(daysFromCivil(y, m, 1))) % 7;
  return first + 7 * (n - 1);
}

// The day of the month of a month's last Sunday.
inline int lastSunday(int y, unsigned m) {
  const int32_t next_first = (m == 12) ? daysFromCivil(y + 1, 1, 1) : daysFromCivil(y, m + 1, 1);
  const int32_t last = next_first - 1;
  const int32_t first = daysFromCivil(y, m, 1);
  return (int)(last - first + 1) - weekday(last);
}

inline bool inDaylightSaving(const Zone& z, uint32_t utc) {
  const int y = yearOfDays((int32_t)(utc / 86400));
  const int64_t t = utc;
  if (z.rule == Rule::US) {
    const int64_t start = (int64_t)daysFromCivil(y, 3, nthSunday(y, 3, 2)) * 86400 + 7200 - z.std_min * 60;
    const int64_t end = (int64_t)daysFromCivil(y, 11, nthSunday(y, 11, 1)) * 86400 + 7200 - (z.std_min + 60) * 60;
    return t >= start && t < end;
  }
  if (z.rule == Rule::EU) {
    const int64_t start = (int64_t)daysFromCivil(y, 3, lastSunday(y, 3)) * 86400 + 3600;
    const int64_t end = (int64_t)daysFromCivil(y, 10, lastSunday(y, 10)) * 86400 + 3600;
    return t >= start && t < end;
  }
  return false;
}

// Minutes to add to UTC at `utc` (Unix seconds) in the zone, daylight saving included.
inline int offsetMinutes(int index, uint32_t utc) {
  const Zone& z = zone(index);
  return z.std_min + (inDaylightSaving(z, utc) ? 60 : 0);
}

inline int64_t localSeconds(int index, uint32_t utc) { return (int64_t)utc + offsetMinutes(index, utc) * 60; }

// "13:07": the time of day at `utc` in the zone.
inline void clock(int index, uint32_t utc, char* out, size_t n) {
  const int64_t local = localSeconds(index, utc);
  const int64_t secs_of_day = ((local % 86400) + 86400) % 86400;
  snprintf(out, n, "%02d:%02d", (int)(secs_of_day / 3600), (int)(secs_of_day / 60 % 60));
}

// "-4:00", "+5:30".
inline void formatOffset(int minutes, char* out, size_t n) {
  const int a = minutes < 0 ? -minutes : minutes;
  snprintf(out, n, "%c%d:%02d", minutes < 0 ? '-' : '+', a / 60, a % 60);
}

inline bool sameLocalDay(int index, uint32_t a, uint32_t b) {
  const int64_t la = localSeconds(index, a), lb = localSeconds(index, b);
  return (la >= 0 ? la / 86400 : (la - 86399) / 86400) == (lb >= 0 ? lb / 86400 : (lb - 86399) / 86400);
}

// A clock reading before 2024 hasn't been set by a phone, GPS or a contact's advert.
inline bool clockSet(uint32_t utc) { return utc >= 1704067200u; }

// A zone for a GPS position in degrees: coarse US and European boundaries, then the
// nearest whole hour anywhere else.
inline int suggest(double lat, double lon) {
  if (lat > 51.0 && lat < 72.0 && lon > -170.0 && lon < -130.0) return kAlaska;
  if (lat > 18.5 && lat < 22.5 && lon > -161.0 && lon < -154.5) return kHawaii;
  if (lat > 24.0 && lat < 49.5 && lon > -125.0 && lon < -66.5) {
    if (lat > 31.3 && lat < 37.0 && lon > -114.8 && lon < -109.05) {
      // Arizona keeps standard time, except the Navajo Nation in its northeast, which
      // keeps Mountain daylight time, and the Hopi land inside that, which doesn't.
      const bool hopi = lat > 35.6 && lat < 36.2 && lon > -111.0 && lon < -110.2;
      const bool navajo = lat > 35.1 && lon > -111.6;
      return (navajo && !hopi) ? kMountain : kArizona;
    }
    if (lon < (lat >= 42.0 ? -117.0 : -114.05)) return kPacific;
    if (lon < -101.5) return kMountain;
    if (lon < (lat >= 37.5 ? -87.5 : -85.4)) return kCentral;
    return kEastern;
  }
  // Great Britain, Ireland and Portugal keep Western European time.
  if ((lat > 49.8 && lat < 61.0 && lon > -11.0 && lon < 1.8) || (lat > 36.8 && lat < 42.2 && lon > -9.6 && lon < -6.2)) {
    return kUk;
  }
  if (lat > 35.0 && lat < 71.5 && lon > -10.0 && lon < 32.0) return lon < 22.0 ? kCentralEu : kEasternEu;
  int hours = (int)lround(lon / 15.0);
  if (hours < -12) hours = -12;
  if (hours > 14) hours = 14;
  return fixedZone(hours * 60);
}

}  // namespace tz
}  // namespace offband
