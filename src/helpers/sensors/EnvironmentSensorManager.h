#pragma once

#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/LocationProvider.h>

class EnvironmentSensorManager : public SensorManager {
protected:
  static const int MAX_ACTIVE_SENSORS = 16;

  // Query function pointer + sub-channel index (for multi-channel sensors like INA3221).
  // Sub-channel is 0 for all single-output sensors.
  struct ActiveSensor {
    void    (*query)(uint8_t channel, uint8_t sub_channel, CayenneLPP& telemetry);
    uint8_t   sub_channel;
  };

  ActiveSensor _active_sensors[MAX_ACTIVE_SENSORS];
  int          _active_sensor_count = 0;
  uint8_t      next_available_channel = TELEM_CHANNEL_SELF + 1;

  bool     gps_detected = false;
  bool     gps_active = false;
  uint32_t gps_update_interval_sec = 1;
  uint32_t _gps_baud = 0;   // #149: locked GPS UART baud (0 = not yet locked)
  #if ENV_INCLUDE_GPS && defined(ESP32)
  // #149: non-blocking auto-baud state machine (Meshtastic-style). One tiny step per
  // loop() while gps_active && !_gps_baud_locked -- never blocks setup(); falls back
  // to the first candidate and locks if nothing answers (no spin/wedge).
  bool     _gps_baud_locked = false;
  uint8_t  _baud_idx = 0;            // candidate index during probe
  uint32_t _baud_window_ms = 0;      // millis() the current candidate window opened (0 = open fresh)
  uint8_t  _nmea_st = 0, _nmea_sum = 0, _nmea_ck = 0;   // incremental "$..*HH" checksum scanner
  int      _nmea_blen = 0;
  void     autoBaudStep();
  bool     nmeaScanByte(char c);
  #endif

  #if ENV_INCLUDE_GPS
  LocationProvider* _location;
  unsigned long _last_gps_clock_sync = 0;   // #152: millis() of last GPS clock-sync (0 = never)
  void start_gps();
  void stop_gps();
  void initBasicGPS();
  #ifdef RAK_BOARD
  void rakGPSInit();
  bool gpsIsAwake(uint8_t ioPin);
  #endif
  #endif

public:
  #if ENV_INCLUDE_GPS
  EnvironmentSensorManager(LocationProvider &location): _location(&location){};
  LocationProvider* getLocationProvider() { return _location; }
  #else
  EnvironmentSensorManager(){};
  #endif
  #if ENV_INCLUDE_GPS
  bool gpsHasFix() { return gps_active && _location != nullptr && _location->isValid(); }
  uint32_t getGpsClockSyncTime() const override { return _last_gps_clock_sync; }   // #152
  size_t getGpsStatusText(char* out, size_t cap) override;   // #149
  #endif
  bool begin() override;
  bool querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) override;
  #if ENV_INCLUDE_GPS || defined(ENV_INCLUDE_BME680_BSEC)
  void loop() override;
  #endif
  int getNumSettings() const override;
  const char* getSettingName(int i) const override;
  const char* getSettingValue(int i) const override;
  bool setSettingValue(const char* name, const char* value) override;
};
