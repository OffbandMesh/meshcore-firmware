#pragma once

// #2032: SafeBoot switches the GPS off before a low-battery hold.
//
// A refused boot never reaches the code that manages the GPS, so whatever the module was
// doing it went on doing. On the QCC badge that was 52-77 mA through the whole hold, from
// the cell the hold was protecting. nRF52 keeps a GPIO's output level through SYSTEM_OFF, so
// driving the enable to its off level before the sleep keeps the GPS off until a boot goes
// ahead and the GPS settings decide again.
//
// The pin and its polarity resolve exactly as MicroNMEALocationProvider.h resolves them at
// runtime -- GPS_EN, else PIN_GPS_EN; GPS_EN_ACTIVE, else PIN_GPS_EN_ACTIVE, else HIGH -- so
// SafeBoot switches off the same line, at the same polarity, that the firmware switches on.

namespace offband {

// The level that switches the GPS off, given the level that switches it on.
constexpr int gpsEnableOffLevel(int active_level) { return active_level ? 0 : 1; }

// A board without a GPS enable declares it as -1 (MicroNMEALocationProvider.h's default).
constexpr bool gpsEnablePinUsable(long pin) { return pin >= 0; }

}  // namespace offband
