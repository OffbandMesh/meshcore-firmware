#pragma once

#include <stdint.h>

// #607: plausibility window for AUTOMATED clock sources only (GPS epoch
// decode, contacts-store RTC bootstrap). Owner-authenticated paths (client
// CMD_SET_DEVICE_TIME, CLI `time` / `clock sync`) are deliberately NOT gated
// here -- owner decision on #607: "direction is never a reason to reject a
// time set; source and sanity are." An automated source may be rejected for
// an implausible VALUE (from either direction); the owner may never be.
namespace offband {

// Floor of the plausibility window: the firmware build date (parsed once from
// OFFBAND_BUILD_DATE "YYYY-MM-DD", injected by scripts/inject_offband_version.py).
// A device cannot legitimately observe a time earlier than its firmware build.
uint32_t buildEpochFloor();

// Ceiling: floor + CLOCK_SANITY_HORIZON_YEARS (20y). Catches GPS week-rollover
// class corruption (~+19.6y per rollover; the #607 car node sat ~+59y out).
uint32_t plausibilityCeiling();

// floor <= t <= ceiling
bool plausibleEpoch(uint32_t t);

// Heal persisted contact lastmod values written under a poisoned clock:
// implausible values collapse to the floor so bootstrapRTCfromContacts can
// never re-poison the RTC from the contacts store (#607 mechanism 2).
uint32_t clampLastmod(uint32_t lastmod);

// Single-line, rate-safe audit print for every ACCEPTED clock set. Rides the
// serial stream, so the #384 caplog tee captures it when capture is enabled.
void logClockSet(const char* source, uint32_t old_epoch, uint32_t new_epoch);

// Rejection audit for AUTOMATED sources only; capped per boot (flood-safe).
void logClockReject(const char* source, uint32_t current, uint32_t attempted);

// Test seam: override the parsed build-date string (host tests only).
void _setBuildDateForTest(const char* yyyy_mm_dd);

}  // namespace offband
