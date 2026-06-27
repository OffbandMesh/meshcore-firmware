#pragma once

#include <CayenneLPP.h>
#include "sensors/LocationProvider.h"

#define TELEM_PERM_BASE         0x01   // 'base' permission includes battery
#define TELEM_PERM_LOCATION     0x02
#define TELEM_PERM_ENVIRONMENT  0x04   // permission to access environment sensors

#define TELEM_CHANNEL_SELF   1   // LPP data channel for 'self' device

// #152: GPS clock-sync (provider-agnostic). A GPS epoch below this (2025-01-01
// UTC) is treated as "no valid time"; avoids chip-specific GPS validity flags.
static const uint32_t      GPS_CLOCK_SANE_MIN       = 1735689600UL;
static const unsigned long GPS_CLOCK_SYNC_INTERVAL  = 1800000UL;   // re-sync every 30 min

class SensorManager {
public:
  double node_lat, node_lon;  // modify these, if you want to affect Advert location
  double node_altitude;       // altitude in meters

  SensorManager() { node_lat = 0; node_lon = 0; node_altitude = 0; }
  virtual bool begin() { return false; }
  virtual bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) { return false; }
  virtual void loop() { }
  virtual int getNumSettings() const { return 0; }
  virtual const char* getSettingName(int i) const { return NULL; }
  virtual const char* getSettingValue(int i) const { return NULL; }
  virtual bool setSettingValue(const char* name, const char* value) { return false; }
  virtual LocationProvider* getLocationProvider() { return NULL; }
  virtual uint32_t getGpsClockSyncTime() const { return 0; }   // #152: millis() of last GPS clock-sync (0 = never)
  // Offband (#149): format live GPS state as ASCII into out[0..cap). Default empty
  // for managers without GPS; EnvironmentSensorManager overrides it.
  virtual size_t getGpsStatusText(char* out, size_t cap) { if (cap > 0) out[0] = '\0'; return 0; }

  // Helper functions to manage setting by keys (useful in many places ...)
  const char* getSettingByKey(const char* key) {
    int num = getNumSettings();
    for (int i = 0; i < num; i++) {
      if (strcmp(getSettingName(i), key) == 0) {
        return getSettingValue(i);
      }
    }
    return NULL;
  }
};
