#include "ClockSanity.h"

#include <stdio.h>
#include <string.h>

#ifdef ARDUINO
  #include <Arduino.h>
#endif

// OFFBAND_BUILD_DATE is injected per-build ("YYYY-MM-DD"). Native/host test
// builds may lack it; fall back to a fixed floor that is still far past any
// plausible stored-clock garbage from the pre-#607 era.
#ifndef OFFBAND_BUILD_DATE
  #define OFFBAND_BUILD_DATE "2026-01-01"
#endif

#define CLOCK_SANITY_HORIZON_YEARS 20
#define SECONDS_PER_YEAR 31557600u  // 365.25d -- coarse is fine at this scale

namespace offband {

static const char* s_build_date = OFFBAND_BUILD_DATE;
static uint32_t s_floor_cache = 0;

// Days-from-civil (Howard Hinnant's algorithm), portable -- no timegm on
// every toolchain we build for.
static uint32_t civilToEpoch(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = (long)era * 146097 + (long)doe - 719468;
  return days <= 0 ? 0u : (uint32_t)days * 86400u;
}

uint32_t buildEpochFloor() {
  if (s_floor_cache) return s_floor_cache;
  int y = 0; unsigned m = 0, d = 0;
  if (sscanf(s_build_date, "%d-%u-%u", &y, &m, &d) == 3 &&
      y >= 2020 && m >= 1 && m <= 12 && d >= 1 && d <= 31) {
    s_floor_cache = civilToEpoch(y, m, d);
  } else {
    s_floor_cache = civilToEpoch(2026, 1, 1);  // parse failure: conservative
  }
  return s_floor_cache;
}

uint32_t plausibilityCeiling() {
  return buildEpochFloor() + CLOCK_SANITY_HORIZON_YEARS * SECONDS_PER_YEAR;
}

bool plausibleEpoch(uint32_t t) {
  return t >= buildEpochFloor() && t <= plausibilityCeiling();
}

uint32_t clampLastmod(uint32_t lastmod) {
  return plausibleEpoch(lastmod) ? lastmod : buildEpochFloor();
}

void logClockSet(const char* source, uint32_t old_epoch, uint32_t new_epoch) {
#ifdef ARDUINO
  // One line per accepted set; sets are rare human/GPS events, so this cannot
  // flood the pipe (SAFELANE rule 10). Serial-stream so caplog tees it.
  // %lld: a correction from a decades-poisoned clock exceeds what 32-bit
  // %ld can represent (Gemini review 2026-08-09).
  Serial.printf("[clock] set by %s: %lu -> %lu (delta %+lld s)\n",
                source, (unsigned long)old_epoch, (unsigned long)new_epoch,
                (long long)((int64_t)new_epoch - (int64_t)old_epoch));
#else
  (void)source; (void)old_epoch; (void)new_epoch;
#endif
}

void logClockReject(const char* source, uint32_t current, uint32_t attempted) {
#ifdef ARDUINO
  // Rejections from AUTOMATED sources can repeat every loop while the bad
  // input persists (e.g. a GPS stuck on a rolled-over week). Cap the total
  // per boot so diagnostics exist without flooding the pipe (SAFELANE 10).
  static uint8_t budget = 3;
  if (budget == 0) return;
  budget--;
  Serial.printf("[clock] REJECTED implausible %s value %lu (clock stays %lu)%s\n",
                source, (unsigned long)attempted, (unsigned long)current,
                budget == 0 ? " [further rejections muted this boot]" : "");
#else
  (void)source; (void)current; (void)attempted;
#endif
}

void _setBuildDateForTest(const char* yyyy_mm_dd) {
  s_build_date = yyyy_mm_dd;
  s_floor_cache = 0;
}

}  // namespace offband
