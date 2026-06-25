#pragma once

#include <Arduino.h>
#include <string.h>

#include <helpers/sensors/MicroNMEALocationProvider.h>

class PhotonGPSLocationProvider : public MicroNMEALocationProvider {
  static const unsigned long POWER_TOGGLE_GUARD_MS = 2000;
  static const unsigned long WAKE_SETTLE_MS = 250;
  static const unsigned long COMMAND_DELAY_MS = 40;
  static const size_t QUIET_COMMAND_COUNT = 10;

  bool _sleeping = false;
  unsigned long _last_power_toggle_ms = 0;
  unsigned long _next_command_at_ms = 0;
  size_t _next_quiet_command = QUIET_COMMAND_COUNT;

  bool powerToggleGuardElapsed() const {
    return _last_power_toggle_ms == 0 || (millis() - _last_power_toggle_ms) >= POWER_TOGGLE_GUARD_MS;
  }

  void sendQueuedCommand(const char *sentence) {
    sendSentence(sentence);
    _next_command_at_ms = millis() + COMMAND_DELAY_MS;
  }

  void scheduleQuietNmea() {
    _next_quiet_command = 0;
    _next_command_at_ms = millis() + WAKE_SETTLE_MS;
  }

  void runDeferredCommands() {
    static const char *const quiet_commands[QUIET_COMMAND_COUNT] = {
      "$PAIR062,0,1", // GGA once per fix
      "$PAIR062,1,0", // GLL off
      "$PAIR062,2,0", // GSA off
      "$PAIR062,3,1", // GSV once per fix for satellites-in-view details
      "$PAIR062,4,1", // RMC once per fix
      "$PAIR062,5,0", // VTG off
      "$PAIR062,6,0", // ZDA off
      "$PAIR062,7,0", // GRS off
      "$PAIR062,8,0", // GST off
      "$PAIR062,9,0", // GNS off
    };
    if (_sleeping || _next_quiet_command >= QUIET_COMMAND_COUNT) {
      return;
    }
    if ((long)(millis() - _next_command_at_ms) < 0) {
      return;
    }
    sendQueuedCommand(quiet_commands[_next_quiet_command++]);
  }

public:
  PhotonGPSLocationProvider(Stream &ser, mesh::RTCClock *clock = NULL)
      : MicroNMEALocationProvider(ser, clock) {}

  void begin() override {
    MicroNMEALocationProvider::begin();

    if (_sleeping && powerToggleGuardElapsed()) {
      sendSentence("$PAIR002");
      _sleeping = false;
      _last_power_toggle_ms = millis();
    }

    scheduleQuietNmea();
  }

  void stop() override {
    if (!_sleeping && powerToggleGuardElapsed()) {
      sendSentence("$PAIR003");
      _sleeping = true;
      _last_power_toggle_ms = millis();
    }

    _next_quiet_command = QUIET_COMMAND_COUNT;
    MicroNMEALocationProvider::stop();
  }

  void loop() override {
    MicroNMEALocationProvider::loop();
    runDeferredCommands();
  }

  bool isEnabled() override {
    return !_sleeping && MicroNMEALocationProvider::isEnabled();
  }
};
